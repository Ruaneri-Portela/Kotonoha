#include "Ddlc/Script.hpp"

#include <algorithm>

namespace Ddlc {

namespace {
const Value kNone;

Node::K kindOf(const std::string& cls) {
    static const std::map<std::string, Node::K> kinds = {
        {"Say", Node::K::Say},           {"Show", Node::K::Show},       {"Scene", Node::K::Scene},
        {"Hide", Node::K::Hide},         {"With", Node::K::With},       {"Jump", Node::K::Jump},
        {"Call", Node::K::Call},         {"Return", Node::K::Return},   {"Pass", Node::K::Pass},
        {"Python", Node::K::Python},     {"EarlyPython", Node::K::EarlyPython},
        {"UserStatement", Node::K::User}, {"Menu", Node::K::Menu},      {"If", Node::K::If},
        {"Label", Node::K::Label},       {"Define", Node::K::Define},   {"Default", Node::K::Default},
        {"Image", Node::K::Image},       {"Transform", Node::K::Transform}, {"Init", Node::K::Init},
        {"Screen", Node::K::Screen},     {"Style", Node::K::Style},     {"ShowLayer", Node::K::ShowLayer},
    };
    const auto dot = cls.rfind('.');
    auto it = kinds.find(dot == std::string::npos ? cls : cls.substr(dot + 1));
    return it == kinds.end() ? Node::K::Other : it->second;
}
} // namespace

const Value& Node::operator[](const char* name) const {
    const Value* v = ast.attr(name);
    return v ? *v : kNone;
}

std::string Node::str(const char* name) const {
    const Value& v = (*this)[name];
    return v.isNone() ? std::string() : v.text();
}

Node* Script::build(const Value& ast, const std::string& file) {
    auto owned = std::make_unique<Node>();
    Node* n = owned.get();
    nodes_.push_back(std::move(owned));
    n->ast = ast;
    n->k = kindOf(ast.cls());
    n->file = file;
    n->line = int((*n)["linenumber"].integer());
    n->route = routes_.at(file);
    switch (n->k) {
    case Node::K::Label:
    case Node::K::Init:
        n->blocks.push_back(buildBlock((*n)["block"], file));
        break;
    case Node::K::If:
        for (const auto& entry : (*n)["entries"].items())
            n->blocks.push_back(entry.items().size() > 1 ? buildBlock(entry.items()[1], file) : std::vector<Node*>{});
        break;
    case Node::K::Menu:
        for (const auto& item : (*n)["items"].items())
            n->blocks.push_back(item.items().size() > 2 ? buildBlock(item.items()[2], file) : std::vector<Node*>{});
        break;
    default: break;
    }
    return n;
}

std::vector<Node*> Script::buildBlock(const Value& list, const std::string& file) {
    std::vector<Node*> out;
    for (const auto& v : list.items())
        if (v.isObj()) out.push_back(build(v, file));
    return out;
}

void Script::chain(std::vector<Node*>& block, Node* after) {
    for (size_t i = 0; i < block.size(); ++i) {
        Node* n = block[i];
        Node* sibling = i + 1 < block.size() ? block[i + 1] : after;
        n->next = sibling;
        switch (n->k) {
        case Node::K::If:
        case Node::K::Menu:
            for (auto& b : n->blocks) chain(b, sibling);
            break;
        case Node::K::Label:
            chain(n->blocks[0], sibling);
            if (!n->blocks[0].empty()) n->next = n->blocks[0][0];
            break;
        case Node::K::Init:
            chain(n->blocks[0], nullptr);
            break;
        default: break;
        }
    }
}

void Script::collect(const std::vector<Node*>& block, int priority, bool init) {
    for (Node* n : block) {
        if (n->k == Node::K::Label) labels_[n->str("name")] = n;
        if (n->k == Node::K::Init) {
            const int p = int(n->ast.get("priority").integer());
            for (Node* c : n->blocks[0]) initOrder_.emplace_back(p, c);
            collect(n->blocks[0], p, true);
            continue;
        }
        if (n->k == Node::K::EarlyPython && !init) initOrder_.emplace_back(-9999, n);
        for (const auto& b : n->blocks) collect(b, priority, init);
    }
}

bool Script::load(const Assets& assets, std::string* error) {
    std::vector<std::string> files;
    for (const auto& name : assets.names())
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".rpyc") == 0) files.push_back(name);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    for (const auto& file : files) {
        Value data;
        try {
            if (!loadRpyc(assets.read(file), data)) {
                if (error) *error = "bad rpyc: " + file;
                return false;
            }
        } catch (const std::exception& e) {
            if (error) *error = file + ": " + e.what();
            return false;
        }
        routes_[file] = static_cast<int>(routes_.size()) + 1;
        const auto& top = data.items();
        if (top.size() < 2) continue;
        auto block = buildBlock(top[1], file);
        chain(block, nullptr);
        collect(block, 0, false);
        ++files_;
    }
    std::stable_sort(initOrder_.begin(), initOrder_.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [_, n] : initOrder_) inits_.push_back(n);
    return !labels_.empty();
}

Node* Script::label(const std::string& name) const {
    auto it = labels_.find(name);
    return it == labels_.end() ? nullptr : it->second;
}

std::map<int, std::vector<std::string>> Script::translationSources() const {
    std::map<int, std::vector<std::string>> result;
    for (const auto& node : nodes_) {
        auto& texts = result[node->route];
        if (node->k == Node::K::Say) {
            const auto text = node->str("what");
            if (!text.empty()) texts.push_back(text);
        } else if (node->k == Node::K::Menu) {
            for (const auto& entry : node->ast.get("items").items()) {
                const auto& items = entry.items();
                if (!items.empty() && items[0].isStr() && !items[0].s.empty())
                    texts.push_back(items[0].s);
            }
        }
    }
    for (auto it = result.begin(); it != result.end();) {
        auto& texts = it->second;
        std::sort(texts.begin(), texts.end());
        texts.erase(std::unique(texts.begin(), texts.end()), texts.end());
        if (texts.empty()) it = result.erase(it);
        else ++it;
    }
    return result;
}

} // namespace Ddlc
