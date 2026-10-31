#include "Ddlc/Python.hpp"
#include "Ddlc/Runtime.hpp"
#include "PythonInternal.hpp"

#include <Python.h>
#include <SDL3/SDL_filesystem.h>
#ifdef __ANDROID__
#include <Kotonoha/core/FileSystem.hpp>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <exception>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace Ddlc {
namespace {

class Gil {
public:
    Gil() : state_(PyGILState_Ensure()) {}
    ~Gil() { PyGILState_Release(state_); }
    Gil(const Gil&) = delete;
    Gil& operator=(const Gil&) = delete;
private:
    PyGILState_STATE state_;
};

class Ref {
public:
    explicit Ref(PyObject* value = nullptr) : value_(value) {}
    ~Ref() { Py_XDECREF(value_); }
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    PyObject* get() const { return value_; }
    PyObject* release() { return std::exchange(value_, nullptr); }
    explicit operator bool() const { return value_ != nullptr; }
private:
    PyObject* value_;
};

void checkPythonStatus(PyStatus status) {
    if (PyStatus_Exception(status))
        throw std::runtime_error(std::string("Cannot initialize CPython: ") +
                                 (status.err_msg ? status.err_msg : "unknown error"));
}

void configureBundledPython(PyConfig& config) {
#if defined(__APPLE__) || defined(__ANDROID__)
#ifdef __ANDROID__
    const auto resources = Kotonoha::resourceDirectory();
#else
    const char* base = SDL_GetBasePath();
    if (!base) throw std::runtime_error("Cannot locate bundled Python resources");
    const auto resources = std::filesystem::path(base);
#endif
    const auto pythonVersion =
        "python" + std::to_string(PY_MAJOR_VERSION) + "." + std::to_string(PY_MINOR_VERSION);
    auto home = resources / "python";
#if defined(__APPLE__) && !TARGET_OS_IPHONE
    auto baseDirectory = resources;
    if (baseDirectory.filename().empty()) baseDirectory = baseDirectory.parent_path();
    const auto contents = baseDirectory.parent_path();
    const bool macBundle = contents.filename() == "Contents" &&
        (baseDirectory.filename() == "MacOS" || baseDirectory.filename() == "Resources");
    if (macBundle) {
        const auto framework = contents / "Frameworks" / "Python.framework" / "Versions" / "Current";
        const auto encodings = framework / "lib" / pythonVersion / "encodings" / "__init__.py";
        home = std::filesystem::is_regular_file(encodings) ? framework : contents / "Frameworks" / "python";
    }
#endif
    if (!std::filesystem::is_directory(home)) {
#if TARGET_OS_IPHONE || defined(__ANDROID__)
        throw std::runtime_error("Bundled Python standard library is missing: " + home.string());
#else
        if (macBundle)
            throw std::runtime_error("Bundled Python standard library is missing: " + home.string());
        return;
#endif
    }

    checkPythonStatus(PyConfig_SetBytesString(&config, &config.home, home.c_str()));
    const auto stdlib = home / "lib" / pythonVersion;
    for (const auto& path : {stdlib, stdlib / "lib-dynload", resources / "app"}) {
        wchar_t* wide = Py_DecodeLocale(path.c_str(), nullptr);
        if (!wide) throw std::runtime_error("Cannot decode Python resource path: " + path.string());
        const PyStatus status = PyWideStringList_Append(&config.module_search_paths, wide);
        PyMem_RawFree(wide);
        checkPythonStatus(status);
    }
    config.module_search_paths_set = 1;
    config.use_environment = 0;
    config.user_site_directory = 0;
    config.write_bytecode = 0;
    config.buffered_stdio = 0;
#else
    (void)config;
#endif
}

void initializePython() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (Py_IsInitialized()) return;
        PyPreConfig preconfig;
        PyPreConfig_InitPythonConfig(&preconfig);
        preconfig.utf8_mode = 1;
        checkPythonStatus(Py_PreInitialize(&preconfig));
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        struct ConfigCleanup {
            PyConfig& config;
            ~ConfigCleanup() { PyConfig_Clear(&config); }
        } cleanup{config};
        config.install_signal_handlers = 0;
        config.parse_argv = 0;
        configureBundledPython(config);
        checkPythonStatus(Py_InitializeFromConfig(&config));
        PyEval_SaveThread();
    });
}

std::string utf8(PyObject* unicode) {
    Ref encoded(PyUnicode_AsEncodedString(unicode, "utf-8", "surrogateescape"));
    if (!encoded) return {};
    char* data = nullptr;
    Py_ssize_t size = 0;
    if (PyBytes_AsStringAndSize(encoded.get(), &data, &size) < 0) return {};
    return {data, static_cast<size_t>(size)};
}

std::string pythonError() {
    PyObject* type = nullptr;
    PyObject* error = nullptr;
    PyObject* traceback = nullptr;
    PyErr_Fetch(&type, &error, &traceback);
    PyErr_NormalizeException(&type, &error, &traceback);
    Ref typeRef(type), errorRef(error), tracebackRef(traceback);
    Ref typeName(type ? PyObject_GetAttrString(type, "__name__") : nullptr);
    Ref message(error ? PyObject_Str(error) : nullptr);
    std::string result = typeName ? utf8(typeName.get()) : "Python error";
    if (message) {
        const std::string text = utf8(message.get());
        if (!text.empty()) result += ": " + text;
    }
    PyErr_Clear();
    return result;
}

void raiseCurrentError() {
    throw PyError(pythonError());
}

struct BridgeContext {
    Interp* owner = nullptr;
};

struct Proxy {
    PyObject_HEAD
    std::shared_ptr<Object>* object;
    std::shared_ptr<BridgeContext>* context;
};

} // namespace

struct PythonValue {
    PyObject* object;
    std::weak_ptr<BridgeContext> context;

    PythonValue(PyObject* value, const std::shared_ptr<BridgeContext>& owner)
        : object(value), context(owner) {
        Py_INCREF(object);
    }
    ~PythonValue() {
        if (Py_IsInitialized()) {
            Gil gil;
            Py_DECREF(object);
        }
    }
    Interp& interpreter() const {
        const auto bridge = context.lock();
        if (!bridge || !bridge->owner)
            throw PyError("Python value outlived its interpreter");
        return *bridge->owner;
    }
};

struct Interp::State {
    explicit State(Interp& interpreter) : owner(interpreter), context(std::make_shared<BridgeContext>()) {
        context->owner = &owner;
        globals = PyDict_New();
        builtinsDict = PyDict_Copy(PyEval_GetBuiltins());
        if (!globals || !builtinsDict) raiseCurrentError();
        if (PyDict_SetItemString(globals, "__builtins__", builtinsDict) < 0)
            raiseCurrentError();

        static PyMethodDef proxyMethods[] = {
            {"__enter__", proxyEnter, METH_NOARGS, nullptr},
            {"__exit__", proxyExit, METH_VARARGS, nullptr},
            {nullptr, nullptr, 0, nullptr}};
        static PyType_Slot basicSlots[] = {
            {Py_tp_dealloc, reinterpret_cast<void*>(deallocateProxy)},
            {Py_tp_getattro, reinterpret_cast<void*>(getProxyAttribute)},
            {Py_tp_setattro, reinterpret_cast<void*>(setProxyAttribute)},
            {Py_tp_repr, reinterpret_cast<void*>(proxyRepr)},
            {Py_tp_iter, reinterpret_cast<void*>(proxyIter)},
            {Py_tp_methods, proxyMethods},
            {0, nullptr}};
        static PyType_Slot callableSlots[] = {
            {Py_tp_dealloc, reinterpret_cast<void*>(deallocateProxy)},
            {Py_tp_getattro, reinterpret_cast<void*>(getProxyAttribute)},
            {Py_tp_setattro, reinterpret_cast<void*>(setProxyAttribute)},
            {Py_tp_repr, reinterpret_cast<void*>(proxyRepr)},
            {Py_tp_iter, reinterpret_cast<void*>(proxyIter)},
            {Py_tp_call, reinterpret_cast<void*>(callProxy)},
            {Py_tp_methods, proxyMethods},
            {0, nullptr}};
        PyType_Spec basic{"kotonoha.RenPyObject", sizeof(Proxy), 0, Py_TPFLAGS_DEFAULT, basicSlots};
        PyType_Spec callable{"kotonoha.RenPyCallable", sizeof(Proxy), 0, Py_TPFLAGS_DEFAULT, callableSlots};
        proxyType = PyType_FromSpec(&basic);
        callableType = PyType_FromSpec(&callable);
        controlError = PyErr_NewException("kotonoha.RenPyControl", PyExc_BaseException, nullptr);
        if (!proxyType || !callableType || !controlError) raiseCurrentError();

        static PyMethodDef importDef{"__import__", importModule, METH_VARARGS, nullptr};
        Ref capsule(PyCapsule_New(this, "kotonoha.Interp", nullptr));
        Ref importer(capsule ? PyCFunction_NewEx(&importDef, capsule.get(), nullptr) : nullptr);
        if (!importer || PyDict_SetItemString(builtinsDict, "__import__", importer.get()) < 0)
            raiseCurrentError();

        for (const auto& alias : {std::pair{"xrange", "range"}, {"long", "int"},
                                  {"unicode", "str"}, {"basestring", "str"}, {"unichr", "chr"}}) {
            PyObject* builtin = PyDict_GetItemString(builtinsDict, alias.second);
            if (!builtin || PyDict_SetItemString(globals, alias.first, builtin) < 0)
                raiseCurrentError();
        }
        Ref helperGlobals(PyDict_New());
        if (!helperGlobals ||
            PyDict_SetItemString(helperGlobals.get(), "__builtins__", PyEval_GetBuiltins()) < 0)
            raiseCurrentError();
        static const char* compatibility = R"py(
import ast
class _LegacyGlobals(ast.NodeTransformer):
    def visit_FunctionDef(self, node):
        self.generic_visit(node)
        names = []
        class Strip(ast.NodeTransformer):
            def visit_FunctionDef(self, inner): return inner
            def visit_AsyncFunctionDef(self, inner): return inner
            def visit_ClassDef(self, inner): return inner
            def visit_Global(self, inner):
                names.extend(inner.names)
                return ast.copy_location(ast.Pass(), inner)
        strip = Strip()
        node.body = [strip.visit(part) for part in node.body]
        if names:
            node.body.insert(0, ast.copy_location(ast.Global(names=list(dict.fromkeys(names))), node))
        return node
    visit_AsyncFunctionDef = visit_FunctionDef
def compile_legacy(source):
    tree = _LegacyGlobals().visit(ast.parse(source, "<Ren'Py>", "exec"))
    return compile(ast.fix_missing_locations(tree), "<Ren'Py>", "exec")
def legacy_range(*args):
    return list(range(*args))
)py";
        Ref initialized(PyRun_String(compatibility, Py_file_input, helperGlobals.get(), helperGlobals.get()));
        if (!initialized) raiseCurrentError();
        legacyCompiler = PyDict_GetItemString(helperGlobals.get(), "compile_legacy");
        if (!legacyCompiler) throw PyError("Cannot initialize the Ren'Py compatibility compiler");
        Py_INCREF(legacyCompiler);
        PyObject* legacyRange = PyDict_GetItemString(helperGlobals.get(), "legacy_range");
        if (!legacyRange || PyDict_SetItemString(builtinsDict, "range", legacyRange) < 0)
            raiseCurrentError();
    }

    ~State() {
        context->owner = nullptr;
        for (auto& [_, code] : codeCache) Py_DECREF(code);
        Py_XDECREF(globals);
        Py_XDECREF(builtinsDict);
        for (auto& [_, proxy] : proxies) Py_DECREF(proxy);
        Py_XDECREF(proxyType);
        Py_XDECREF(callableType);
        Py_XDECREF(controlError);
        Py_XDECREF(legacyCompiler);
    }

    Interp& owner;
    std::shared_ptr<BridgeContext> context;
    PyObject* globals = nullptr;
    PyObject* builtinsDict = nullptr;
    PyObject* proxyType = nullptr;
    PyObject* callableType = nullptr;
    PyObject* controlError = nullptr;
    PyObject* legacyCompiler = nullptr;
    std::unordered_map<Object*, PyObject*> proxies;
    std::unordered_map<std::string, PyObject*> codeCache;
    std::unordered_map<std::string, Value> modules;
    std::exception_ptr pending;

    static State& state(Proxy* proxy) {
        if (!proxy->context || !*proxy->context || !(*proxy->context)->owner)
            throw PyError("Ren'Py object outlived its interpreter");
        return *(*proxy->context)->owner->state_;
    }

    static void setCppError(const std::exception& error) {
        const std::string message = error.what();
        PyObject* type = PyExc_RuntimeError;
        if (message.rfind("IOError:", 0) == 0 || message.rfind("OSError:", 0) == 0)
            type = PyExc_OSError;
        else if (message.rfind("ValueError:", 0) == 0)
            type = PyExc_ValueError;
        else if (message.rfind("TypeError:", 0) == 0)
            type = PyExc_TypeError;
        else if (message.rfind("AttributeError:", 0) == 0)
            type = PyExc_AttributeError;
        else if (message.rfind("KeyError:", 0) == 0)
            type = PyExc_KeyError;
        else if (message.rfind("IndexError:", 0) == 0)
            type = PyExc_IndexError;
        PyErr_SetString(type, message.c_str());
    }

    static void deallocateProxy(PyObject* self) {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        delete proxy->object;
        delete proxy->context;
        PyTypeObject* type = Py_TYPE(self);
        type->tp_free(self);
        Py_DECREF(type);
    }

    static PyObject* getProxyAttribute(PyObject* self, PyObject* name) {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        try {
            State& bridge = state(proxy);
            const std::string key = utf8(name);
            if (PyErr_Occurred()) return nullptr;
            auto& object = **proxy->object;
            const auto found = object.attrs.find(key);
            if (found != object.attrs.end()) return bridge.toPython(found->second);
            if (key.size() >= 4 && key.compare(0, 2, "__") == 0)
                return PyObject_GenericGetAttr(self, name);
            if (object.openAttrs) Py_RETURN_NONE;
            PyErr_Format(PyExc_AttributeError, "%s has no attribute '%s'",
                         object.cls.c_str(), key.c_str());
        } catch (const std::exception& error) {
            setCppError(error);
        }
        return nullptr;
    }

    static int setProxyAttribute(PyObject* self, PyObject* name, PyObject* value) {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        try {
            State& bridge = state(proxy);
            const std::string key = utf8(name);
            if (PyErr_Occurred()) return -1;
            if (value) (**proxy->object).attrs[key] = bridge.fromPython(value);
            else (**proxy->object).attrs.erase(key);
            return 0;
        } catch (const std::exception& error) {
            setCppError(error);
            return -1;
        }
    }

    static PyObject* proxyRepr(PyObject* self) {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        const std::string text = "<Ren'Py " + (**proxy->object).cls + ">";
        return PyUnicode_FromStringAndSize(text.data(), static_cast<Py_ssize_t>(text.size()));
    }

    static PyObject* proxyIter(PyObject* self) {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        try {
            State& bridge = state(proxy);
            const auto found = (**proxy->object).attrs.find("__lines__");
            if (found == (**proxy->object).attrs.end()) {
                PyErr_SetString(PyExc_TypeError, "Ren'Py object is not iterable");
                return nullptr;
            }

            Ref items(bridge.toPython(found->second));
            return items ? PyObject_GetIter(items.get()) : nullptr;
        } catch (const std::exception& error) {
            setCppError(error);
            return nullptr;
        }
    }

    static PyObject* proxyEnter(PyObject* self, PyObject*) {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        if ((**proxy->object).cls != "file") {
            PyErr_SetString(PyExc_TypeError, "Ren'Py object is not a context manager");
            return nullptr;
        }
        Py_INCREF(self);
        return self;
    }

    static PyObject* proxyExit(PyObject* self, PyObject*) {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        try {
            State& bridge = state(proxy);
            const auto found = (**proxy->object).attrs.find("close");
            if (found == (**proxy->object).attrs.end())
                throw PyError("IOError: virtual file has no close operation");
            bridge.owner.call(found->second, {});
            Py_RETURN_FALSE;
        } catch (const std::exception& error) {
            setCppError(error);
            return nullptr;
        }
    }

    static PyObject* callProxy(PyObject* self, PyObject* args, PyObject* kwargs) {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        State* bridge = nullptr;
        try {
            bridge = &state(proxy);
            const NativeFn& function = (**proxy->object).fn;
            if (!function) {
                PyErr_SetString(PyExc_TypeError, "Ren'Py object is not callable");
                return nullptr;
            }
            Args positional;
            const Py_ssize_t count = PyTuple_Size(args);
            if (count < 0) return nullptr;
            for (Py_ssize_t i = 0; i < count; ++i)
                positional.push_back(bridge->fromPython(PyTuple_GetItem(args, i)));
            Kwargs named;
            if (kwargs) {
                PyObject *key = nullptr, *value = nullptr;
                Py_ssize_t pos = 0;
                while (PyDict_Next(kwargs, &pos, &key, &value))
                    named.emplace_back(utf8(key), bridge->fromPython(value));
            }
            return bridge->toPython(function(bridge->owner, positional, named));
        } catch (const JumpSignal&) {
            bridge->pending = std::current_exception();
        } catch (const QuitSignal&) {
            bridge->pending = std::current_exception();
        } catch (const RestartSignal&) {
            bridge->pending = std::current_exception();
        } catch (const AbortSignal&) {
            bridge->pending = std::current_exception();
        } catch (const std::exception& error) {
            setCppError(error);
            return nullptr;
        }
        if (bridge) PyErr_SetString(bridge->controlError, "Ren'Py control flow");
        return nullptr;
    }

    static PyObject* importModule(PyObject* self, PyObject* args) {
        auto* bridge = static_cast<State*>(PyCapsule_GetPointer(self, "kotonoha.Interp"));
        if (!bridge) return nullptr;
        const char* name = nullptr;
        PyObject *globals = nullptr, *locals = nullptr, *fromList = nullptr;
        int level = 0;
        if (!PyArg_ParseTuple(args, "s|OOOi:__import__", &name, &globals, &locals, &fromList, &level))
            return nullptr;
        if (level != 0) {
            PyErr_SetString(PyExc_ImportError, "Relative imports are not supported by the Ren'Py bridge");
            return nullptr;
        }
        const auto found = bridge->modules.find(name);
        if (found == bridge->modules.end()) {
            PyErr_Format(PyExc_ImportError, "Module '%s' is unavailable in the Ren'Py bridge", name);
            return nullptr;
        }
        try {
            return bridge->toPython(found->second);
        } catch (const std::exception& error) {
            setCppError(error);
            return nullptr;
        }
    }

    PyObject* toPython(const Value& value) {
        switch (value.t) {
        case Value::T::None: Py_INCREF(Py_None); return Py_None;
        case Value::T::Bool: return PyBool_FromLong(value.b);
        case Value::T::Int: return PyLong_FromLongLong(value.i);
        case Value::T::Float: return PyFloat_FromDouble(value.f);
        case Value::T::Str:
            return PyUnicode_DecodeUTF8(value.s.data(), static_cast<Py_ssize_t>(value.s.size()), "surrogateescape");
        case Value::T::Obj: break;
        }
        Object& object = *value.o;
        if (object.python) {
            Py_INCREF(object.python->object);
            return object.python->object;
        }
        if (object.k == Object::K::List || object.k == Object::K::Tuple) {
            const bool tuple = object.k == Object::K::Tuple;
            const Py_ssize_t n = static_cast<Py_ssize_t>(object.items.size());
            Ref result(tuple ? PyTuple_New(n) : PyList_New(n));
            if (!result) return nullptr;
            object.python = std::make_shared<PythonValue>(result.get(), context);
            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject* item = toPython(object.items[static_cast<size_t>(i)]);
                if (!item) return nullptr;
                if (tuple) PyTuple_SET_ITEM(result.get(), i, item);
                else PyList_SET_ITEM(result.get(), i, item);
            }
            return result.release();
        }
        if (object.k == Object::K::Dict) {
            Ref result(PyDict_New());
            if (!result) return nullptr;
            object.python = std::make_shared<PythonValue>(result.get(), context);
            for (const auto& [key, item] : object.dict) {
                Ref k(toPython(key)), v(toPython(item));
                if (!k || !v || PyDict_SetItem(result.get(), k.get(), v.get()) < 0)
                    return nullptr;
            }
            return result.release();
        }
        if (object.k == Object::K::Class && !object.fn) {
            Ref name(PyUnicode_FromString(object.cls.c_str()));
            Ref bases(PyTuple_Pack(1, reinterpret_cast<PyObject*>(&PyBaseObject_Type)));
            Ref dict(PyDict_New());
            Ref result(name && bases && dict
                           ? PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyType_Type),
                                                          name.get(), bases.get(), dict.get(), nullptr)
                           : nullptr);
            if (!result) return nullptr;
            object.python = std::make_shared<PythonValue>(result.get(), context);
            return result.release();
        }
        const auto found = proxies.find(&object);
        if (found != proxies.end()) {
            Py_INCREF(found->second);
            return found->second;
        }
        auto* type = reinterpret_cast<PyTypeObject*>(object.fn ? callableType : proxyType);
        auto* proxy = reinterpret_cast<Proxy*>(type->tp_alloc(type, 0));
        if (!proxy) return nullptr;
        proxy->object = new ObjRef(value.o);
        proxy->context = new std::shared_ptr<BridgeContext>(context);
        PyObject* result = reinterpret_cast<PyObject*>(proxy);
        Py_INCREF(result);
        proxies.emplace(&object, result);
        return result;
    }

    Value fromPython(PyObject* value) {
        if (!value) raiseCurrentError();
        if (value == Py_None) return {};
        if (PyBool_Check(value)) return Value::boolean(value == Py_True);
        if (PyLong_Check(value)) {
            const long long n = PyLong_AsLongLong(value);
            if (PyErr_Occurred()) raiseCurrentError();
            return Value::integer(n);
        }
        if (PyFloat_Check(value)) return Value::real(PyFloat_AsDouble(value));
        if (PyUnicode_Check(value)) {
            const std::string text = utf8(value);
            if (PyErr_Occurred()) raiseCurrentError();
            return Value::str(text);
        }
        if (PyBytes_Check(value)) {
            char* data = nullptr;
            Py_ssize_t size = 0;
            if (PyBytes_AsStringAndSize(value, &data, &size) < 0) raiseCurrentError();
            return Value::str(std::string(data, static_cast<size_t>(size)));
        }
        if (PyObject_TypeCheck(value, reinterpret_cast<PyTypeObject*>(proxyType)) ||
            PyObject_TypeCheck(value, reinterpret_cast<PyTypeObject*>(callableType)))
            return Value::object(*reinterpret_cast<Proxy*>(value)->object);

        Object::K kind = Object::K::Instance;
        if (PyList_Check(value)) kind = Object::K::List;
        else if (PyTuple_Check(value)) kind = Object::K::Tuple;
        else if (PyDict_Check(value)) kind = Object::K::Dict;
        else if (PyType_Check(value)) kind = Object::K::Class;
        else if (PyCallable_Check(value)) kind = Object::K::Function;
        auto object = makeObj(kind, Py_TYPE(value)->tp_name);
        object->python = std::make_shared<PythonValue>(value, context);
        if (PyCallable_Check(value)) {
            const auto reference = object->python;
            object->fn = [reference](Interp& interpreter, Args& args, Kwargs& kwargs) {
                if (&reference->interpreter() != &interpreter)
                    throw PyError("Cannot call a Python value from another interpreter");
                Gil gil;
                return interpreter.state_->invoke(reference->object, args, kwargs);
            };
        }
        return Value::object(object);
    }

    void checkResult(PyObject* value) {
        if (value) return;
        if (pending) {
            auto signal = std::exchange(pending, {});
            PyErr_Clear();
            std::rethrow_exception(signal);
        }
        raiseCurrentError();
    }

    Value invoke(PyObject* function, const Args& args, const Kwargs& kwargs) {
        Ref positional(PyTuple_New(static_cast<Py_ssize_t>(args.size())));
        Ref named(PyDict_New());
        if (!positional || !named) raiseCurrentError();
        for (size_t i = 0; i < args.size(); ++i) {
            PyObject* argument = toPython(args[i]);
            if (!argument) raiseCurrentError();
            PyTuple_SET_ITEM(positional.get(), static_cast<Py_ssize_t>(i), argument);
        }
        for (const auto& [name, value] : kwargs) {
            Ref argument(toPython(value));
            if (!argument || PyDict_SetItemString(named.get(), name.c_str(), argument.get()) < 0)
                raiseCurrentError();
        }
        Ref result(PyObject_Call(function, positional.get(), named.get()));
        checkResult(result.get());
        return fromPython(result.get());
    }

    Ref localsFor(const ScopeP& scope) {
        if (!scope) return Ref();
        Ref result(PyDict_New());
        if (!result) raiseCurrentError();
        std::vector<const Scope*> scopes;
        for (const Scope* current = scope.get(); current; current = current->parent.get())
            scopes.push_back(current);
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            for (const auto& [name, value] : (*it)->vars) {
                Ref item(toPython(value));
                if (!item || PyDict_SetItemString(result.get(), name.c_str(), item.get()) < 0)
                    raiseCurrentError();
            }
        return result;
    }

    Value run(const std::string& source, bool expression, const ScopeP& scope) {
        const std::string key = (expression ? "E:" : "S:") + source;
        auto found = codeCache.find(key);
        if (found == codeCache.end()) {
            PyObject* code = Py_CompileString(source.c_str(), "<Ren'Py>",
                                              expression ? Py_eval_input : Py_file_input);
            if (!code && !expression && PyErr_ExceptionMatches(PyExc_SyntaxError)) {
                PyObject *type = nullptr, *error = nullptr, *trace = nullptr;
                PyErr_Fetch(&type, &error, &trace);
                Ref typeRef(type), errorRef(error), traceRef(trace);
                Ref message(error ? PyObject_Str(error) : nullptr);
                if (message && utf8(message.get()).find("is used prior to global declaration") != std::string::npos) {
                    PyErr_Clear();
                    Ref input(PyUnicode_DecodeUTF8(source.data(), static_cast<Py_ssize_t>(source.size()),
                                                   "surrogateescape"));
                    if (input) code = PyObject_CallFunctionObjArgs(legacyCompiler, input.get(), nullptr);
                } else {
                    PyErr_Restore(typeRef.release(), errorRef.release(), traceRef.release());
                }
            }
            if (!code) raiseCurrentError();
            found = codeCache.emplace(key, code).first;
        }
        Ref locals(localsFor(scope));
        Ref result(PyEval_EvalCode(found->second, globals, locals ? locals.get() : globals));
        checkResult(result.get());
        if (scope && !expression) {
            PyObject *keyObject = nullptr, *value = nullptr;
            Py_ssize_t position = 0;
            while (PyDict_Next(locals.get(), &position, &keyObject, &value))
                scope->vars[utf8(keyObject)] = fromPython(value);
        }
        return fromPython(result.get());
    }
};

Interp::Interp() {
    initializePython();
    Gil gil;
    state_ = std::make_unique<State>(*this);
    for (const char* name : {"math", "random", "datetime", "time"}) {
        Ref module(PyImport_ImportModule(name));
        if (!module) raiseCurrentError();
        Value value = state_->fromPython(module.get());
        builtins[name] = value;
        registerModule(name, value);
        set(name, value);
    }
    for (const char* name : {"os", "subprocess", "sys"}) {
        Value value = nameSpace(name);
        builtins[name] = value;
        registerModule(name, value);
    }
}

Interp::~Interp() {
    if (state_) {
        Gil gil;
        state_.reset();
    }
}

Value Interp::eval(const std::string& source, const ScopeP& locals) {
    Gil gil;
    return state_->run(source, true, locals);
}

void Interp::exec(const std::string& source, const ScopeP& locals) {
    Gil gil;
    (void)state_->run(source, false, locals);
}

Value Interp::call(const Value& function, Args args, Kwargs kwargs) {
    if (!function.isObj() || !function.o->fn)
        throw PyError("TypeError: object is not callable: " + function.repr());
    return function.o->fn(*this, args, kwargs);
}

Value Interp::lookup(const std::string& name, const ScopeP& locals) const {
    for (auto scope = locals; scope; scope = scope->parent) {
        const auto found = scope->vars.find(name);
        if (found != scope->vars.end()) return found->second;
    }
    Gil gil;
    PyObject* value = PyDict_GetItemString(state_->globals, name.c_str());
    if (!value) value = PyDict_GetItemString(state_->builtinsDict, name.c_str());
    if (!value) throw PyError("NameError: " + name);
    return state_->fromPython(value);
}

bool Interp::has(const std::string& name) const {
    Gil gil;
    return PyDict_GetItemString(state_->globals, name.c_str()) != nullptr ||
           PyDict_GetItemString(state_->builtinsDict, name.c_str()) != nullptr;
}

void Interp::set(const std::string& name, const Value& value) {
    Gil gil;
    Ref item(state_->toPython(value));
    if (!item || PyDict_SetItemString(state_->globals, name.c_str(), item.get()) < 0)
        raiseCurrentError();
}

void Interp::def(const std::string& name, NativeFn fn) {
    set(name, makeFunction(name, std::move(fn)));
}

void Interp::registerModule(const std::string& name, const Value& module) {
    state_->modules[name] = module;
}

Value Interp::nameSpace(const std::string& name) {
    Value value = makeInstance(name);
    value.o->openAttrs = true;
    return value;
}

Value Interp::getAttr(const Value& object, const std::string& name) {
    if (object.isObj() && !object.o->python) {
        if (const Value* attribute = object.attr(name)) return *attribute;
        if (object.o->openAttrs) return {};
    }
    Gil gil;
    Ref value(state_->toPython(object));
    Ref attribute(value ? PyObject_GetAttrString(value.get(), name.c_str()) : nullptr);
    if (!attribute) raiseCurrentError();
    return state_->fromPython(attribute.get());
}

void Interp::setAttr(const Value& object, const std::string& name, const Value& value) {
    if (object.isObj() && !object.o->python) {
        object.o->attrs[name] = value;
        return;
    }
    Gil gil;
    Ref target(state_->toPython(object)), attribute(state_->toPython(value));
    if (!target || !attribute || PyObject_SetAttrString(target.get(), name.c_str(), attribute.get()) < 0)
        raiseCurrentError();
}

Value Interp::getItem(const Value& object, const Value& key) {
    if (object.isObj() && !object.o->python && object.o->k == Object::K::Dict) {
        if (Value* value = object.o->findKey(key)) return *value;
        throw PyError("KeyError: " + key.repr());
    }
    if (object.isObj() && !object.o->python &&
        (object.o->k == Object::K::List || object.o->k == Object::K::Tuple)) {
        int64_t index = key.integer();
        if (index < 0) index += static_cast<int64_t>(object.o->items.size());
        if (index < 0 || static_cast<size_t>(index) >= object.o->items.size())
            throw PyError("IndexError: index out of range");
        return object.o->items[static_cast<size_t>(index)];
    }
    Gil gil;
    Ref target(state_->toPython(object)), index(state_->toPython(key));
    Ref value(target && index ? PyObject_GetItem(target.get(), index.get()) : nullptr);
    if (!value) raiseCurrentError();
    return state_->fromPython(value.get());
}

void Interp::setItem(const Value& object, const Value& key, const Value& value) {
    if (object.isObj() && !object.o->python && object.o->k == Object::K::Dict) {
        if (Value* slot = object.o->findKey(key)) *slot = value;
        else object.o->dict.emplace_back(key, value);
        return;
    }
    if (object.isObj() && !object.o->python && object.o->k == Object::K::List) {
        int64_t index = key.integer();
        if (index < 0) index += static_cast<int64_t>(object.o->items.size());
        if (index < 0 || static_cast<size_t>(index) >= object.o->items.size())
            throw PyError("IndexError: index out of range");
        object.o->items[static_cast<size_t>(index)] = value;
        return;
    }
    Gil gil;
    Ref target(state_->toPython(object)), index(state_->toPython(key)), item(state_->toPython(value));
    if (!target || !index || !item || PyObject_SetItem(target.get(), index.get(), item.get()) < 0)
        raiseCurrentError();
}

std::vector<Value> Interp::iterate(const Value& value) {
    if (value.isObj() && !value.o->python) {
        if (value.o->k == Object::K::List || value.o->k == Object::K::Tuple)
            return value.o->items;
        if (value.o->k == Object::K::Dict) {
            std::vector<Value> result;
            for (const auto& [key, _] : value.o->dict) result.push_back(key);
            return result;
        }
    }
    Gil gil;
    Ref object(state_->toPython(value));
    Ref iter(object ? PyObject_GetIter(object.get()) : nullptr);
    if (!iter) raiseCurrentError();
    std::vector<Value> result;
    while (PyObject* next = PyIter_Next(iter.get())) {
        Ref item(next);
        result.push_back(state_->fromPython(item.get()));
    }
    if (PyErr_Occurred()) raiseCurrentError();
    return result;
}

bool pythonTruthy(const PythonValue& value) {
    Gil gil;
    const int result = PyObject_IsTrue(value.object);
    if (result < 0) raiseCurrentError();
    return result != 0;
}

std::string pythonString(const PythonValue& value, bool repr) {
    Gil gil;
    Ref result(repr ? PyObject_Repr(value.object) : PyObject_Str(value.object));
    if (!result) raiseCurrentError();
    const std::string text = utf8(result.get());
    if (PyErr_Occurred()) raiseCurrentError();
    return text;
}

const Value* pythonAttribute(Object& object, const std::string& name) {
    Gil gil;
    Ref value(PyObject_GetAttrString(object.python->object, name.c_str()));
    if (!value && PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        object.attrs.erase(name);
        return nullptr;
    }
    if (!value) raiseCurrentError();
    Interp& interpreter = object.python->interpreter();
    object.attrs[name] = interpreter.state_->fromPython(value.get());
    return &object.attrs[name];
}

void pythonRefreshItems(Object& object) {
    if (object.k != Object::K::List && object.k != Object::K::Tuple) return;
    Gil gil;
    PyObject* sequence = object.python->object;
    const Py_ssize_t count = PySequence_Size(sequence);
    if (count < 0) raiseCurrentError();
    Interp& interpreter = object.python->interpreter();
    std::vector<Value> result;
    result.reserve(static_cast<size_t>(count));
    for (Py_ssize_t i = 0; i < count; ++i) {
        Ref item(PySequence_GetItem(sequence, i));
        if (!item) raiseCurrentError();
        result.push_back(interpreter.state_->fromPython(item.get()));
    }
    object.items = std::move(result);
}

void pythonRefreshDict(Object& object) {
    if (object.k != Object::K::Dict) return;
    Gil gil;
    Interp& interpreter = object.python->interpreter();
    std::vector<std::pair<Value, Value>> result;
    PyObject *key = nullptr, *value = nullptr;
    Py_ssize_t position = 0;
    while (PyDict_Next(object.python->object, &position, &key, &value))
        result.emplace_back(interpreter.state_->fromPython(key), interpreter.state_->fromPython(value));
    object.dict = std::move(result);
}

PythonThreadRelease::PythonThreadRelease() {
    if (Py_IsInitialized() && PyGILState_Check()) state_ = PyEval_SaveThread();
}

PythonThreadRelease::~PythonThreadRelease() {
    if (state_) PyEval_RestoreThread(static_cast<PyThreadState*>(state_));
}

std::vector<uint32_t> utf8Decode(const std::string& s) {
    std::vector<uint32_t> result;
    for (size_t i = 0; i < s.size();) {
        const auto first = static_cast<uint8_t>(s[i]);
        uint32_t cp = first;
        size_t length = 1;
        if (first >= 0xF0) { cp = first & 0x07; length = 4; }
        else if (first >= 0xE0) { cp = first & 0x0F; length = 3; }
        else if (first >= 0xC0) { cp = first & 0x1F; length = 2; }
        for (size_t j = 1; j < length && i + j < s.size(); ++j)
            cp = (cp << 6) | (static_cast<uint8_t>(s[i + j]) & 0x3F);
        result.push_back(cp);
        i += length;
    }
    return result;
}

std::string utf8Encode(const std::vector<uint32_t>& values) {
    std::string result;
    for (const uint32_t cp : values) {
        if (cp < 0x80) result += char(cp);
        else if (cp < 0x800) {
            result += char(0xC0 | cp >> 6);
            result += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += char(0xE0 | cp >> 12);
            result += char(0x80 | (cp >> 6 & 0x3F));
            result += char(0x80 | (cp & 0x3F));
        } else {
            result += char(0xF0 | cp >> 18);
            result += char(0x80 | (cp >> 12 & 0x3F));
            result += char(0x80 | (cp >> 6 & 0x3F));
            result += char(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

size_t utf8Length(const std::string& s) {
    return static_cast<size_t>(std::count_if(s.begin(), s.end(),
        [](unsigned char ch) { return (ch & 0xC0) != 0x80; }));
}

} // namespace Ddlc
