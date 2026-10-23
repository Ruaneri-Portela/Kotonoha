#pragma once
// The Ren'Py 6.99 runtime of the port: displayables, ATL transforms,
// transitions, the scene lists and the script executor. Rendering produces
// draw lists (no SDL); the app turns them into textured quads.
#include "Ddlc/Python.hpp"
#include "Ddlc/Script.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace Ddlc {

constexpr double kScreenW = 1280, kScreenH = 720;

// A Ren'Py position: integers are pixels, floats a fraction of the area.
struct Pos {
    double px = 0, fr = 0;
    bool set = false;
    double at(double size) const { return px + fr * size; }
    static Pos of(const Value& v) {
        Pos p;
        p.set = true;
        if (v.t == Value::T::Float) p.fr = v.f;
        else p.px = v.num();
        return p;
    }
    static Pos frac(double f) { Pos p; p.set = true; p.fr = f; return p; }
};

struct TextStyle {
    std::string font = "gui/font/Aller_Rg.ttf";
    float size = 24;
    uint32_t color = 0xffffffff; // RRGGBBAA
    std::vector<std::pair<float, uint32_t>> outlines; // (width, colour)
    float maxWidth = 0;   // wrapping width, 0 = none
    float align = 0;      // text_align
    float lineSpacing = 0;
    bool operator==(const TextStyle&) const = default;
};

struct DrawOp {
    enum class K { Image, Solid, Text } k = K::Image;
    std::string image;     // asset path (Image)
    uint32_t color = 0;    // Solid colour (RRGGBBAA)
    std::string text;      // Text (Ren'Py text tags allowed)
    TextStyle style;
    float w = 0, h = 0;    // source size
    float m[6] = {1, 0, 0, 0, 1, 0}; // source pixel -> screen: x = m0 u + m1 v + m2, y = m3 u + m4 v + m5
    float alpha = 1;
    float crop[4] = {0, 0, 0, 0};    // source sub-rectangle (w = 0: whole)
};

struct Render {
    double w = 0, h = 0;
    std::vector<DrawOp> ops;
    Pos xpos, ypos, xanchor, yanchor;
    double xoffset = 0, yoffset = 0;
    // Appends `r` translated by (x, y) with its alpha multiplied.
    void blit(const Render& r, double x, double y, double alpha = 1);
};

class Vm;
struct Disp;
using DispP = std::shared_ptr<Disp>;

struct Disp : std::enable_shared_from_this<Disp> {
    virtual ~Disp() = default;
    virtual Render render(Vm& vm, double st, double at) = 0;
    // A fresh copy for displayables with per-show state (ATL).
    virtual DispP instance() { return shared_from_this(); }
};

// The ATL / Transform machinery.
struct AtlTemplate;
struct TransformState {
    Pos xpos, ypos, xanchor, yanchor;
    double xoffset = 0, yoffset = 0, zoom = 1, xzoom = 1, yzoom = 1, alpha = 1, rotate = 0;
    bool rotateSet = false;
    int xtile = 1, ytile = 1;
    double crop[4] = {0, 0, 0, 0};
    bool cropSet = false;
};
struct Transition;

class TransformDisp : public Disp {
public:
    explicit TransformDisp(std::shared_ptr<AtlTemplate> templ);
    ~TransformDisp() override;
    Render render(Vm& vm, double st, double at) override;
    DispP instance() override;

    void setChild(const DispP& child, double st, std::shared_ptr<Transition> trans = nullptr);
    DispP child() const { return child_; }
    // Replacing a shown transform continues from the old one's properties.
    void takeState(const TransformDisp& old) { state = old.state; }
    void event(const std::string& name) { events_.push_back(name); }
    bool handles(const std::string& event) const;
    bool hideDone() const { return hideDone_; }
    void markHideDone() { hideDone_ = true; }

    TransformState state;
    bool defaultPlacement = false; // the layer's default transform (center, bottom)
    double lastSt = -1;            // the time the ATL ran to last

    struct Run;
    std::shared_ptr<AtlTemplate> templ_;
    std::unique_ptr<Run> run_;
    DispP child_, oldChild_;
    std::shared_ptr<Transition> childTrans_;
    double childBase_ = 0, oldChildBase_ = 0, childChangeSt_ = 0;
    std::vector<std::string> events_;
    bool hideDone_ = false;
    bool started_ = false;
};

struct AtlTemplate {
    Value atl;                                      // renpy.atl.RawBlock (or None)
    std::vector<std::pair<std::string, std::string>> params; // name, default expression (or "")
    std::map<std::string, Value> bound;             // bound parameters
    std::vector<std::pair<std::string, Value>> props; // plain Transform properties
    DispP child;
    std::string name;
};

struct Transition {
    enum class K { Dissolve, ImageDissolve, Pause, Multiple } k = K::Dissolve;
    double time = 0;
    bool alpha = false;
    std::string mask; // ImageDissolve
    double ramp = 8;
    bool reverse = false;
    // Multiple: scenes (False = old, True = new, or a displayable) between transitions.
    struct SceneRef { int kind = 0; DispP disp; }; // 0 old, 1 new, 2 displayable
    std::vector<SceneRef> scenes;
    std::vector<std::shared_ptr<Transition>> steps;
    double duration() const;
};

// Ren'Py text with tags parsed into reveal-timed glyphs.
struct Glyph {
    std::string ch;        // one UTF-8 code point ("\n" for breaks)
    bool italic = false, bold = false;
    bool hasColor = false;
    uint32_t color = 0;
    float space = 0;       // {space=N} advance before the glyph
    double t = 0;          // reveal time offset (seconds) from the start of the text
};
struct RichText {
    std::vector<Glyph> glyphs;
    // Wait points: glyph count and kind (0 click {w}, 1 timed {w=x}, 2 no-wait {nw}).
    struct Wait { size_t at; int kind; double time; };
    std::vector<Wait> waits;
};
RichText parseRich(const std::string& text, double cps);
std::string stripTags(const std::string& text);
uint32_t parseColor(const std::string& s);

DispP makeImageDisp(const std::string& file);
DispP makeSolidDisp(uint32_t color);
DispP makeNullDisp(double w, double h);
DispP makeTextDisp(const std::string& text, const TextStyle& style, const Value& xalign, const Value& yalign);
DispP makeCompositeDisp(double w, double h, std::vector<std::tuple<double, double, DispP>> parts, bool live);
DispP makeTileDisp(const DispP& child);
DispP makeSwitchDisp(std::vector<std::pair<std::string, DispP>> cases);

struct Shown {
    std::string tag;
    std::vector<std::string> name;
    std::shared_ptr<TransformDisp> xf;    // outermost transform
    std::shared_ptr<TransformDisp> inner; // the transform holding the image
    double shownAt = 0;
    int zorder = 0;
    bool hiding = false;
};

struct Window {
    bool visible = false;
    std::string who;       // speaker name (empty: narrator)
    std::string what;      // tagged text
    RichText rich;
    size_t from = 0, to = 0; // glyphs [from, to) reveal in this segment
    double t0 = 0;           // when the segment started revealing
    bool monika = false;     // textbox_monika background
    size_t visibleAt(double now) const;
};

struct ScreenState {
    std::string name; // quick_menu, poem, dialog, confirm, choice, poemgame, tear
    Args args;
    Kwargs kw;
    double shownAt = 0;
};

struct Scene {
    std::vector<Shown> master;
    Window window;
    std::vector<ScreenState> screens;
    bool hasScreen(const std::string& n) const;
};

struct Interaction {
    enum class K { None, Say, Pause, Transition, Menu, Screen, Ui, End, Restart } k = K::None;
    double start = 0, deadline = -1; // seconds; deadline < 0: none
    bool dismiss = true;             // a click ends it
    std::shared_ptr<Transition> trans;
    Scene from;                      // Transition: the old scene
    struct Item {
        std::string text;
        Value value;
        bool enabled = true;
        float x = -1, y = -1;
    };
    std::vector<Item> items;
    std::vector<Item> texts;          // Ui: ui.text labels (value = style name)
    int sayWait = 0;                  // Say: 0 click, 1 timed ({w=x}), 2 none ({nw})
    double sayTime = 0;
    std::string screen;               // Screen / Ui / Menu screen name
    std::string message;
    Args args;
    Kwargs kw;
};

// Services of the host (asset sizes, audio, persistence).
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool imageSize(const std::string& file, int& w, int& h) = 0;
    virtual void textSize(const std::string& text, const TextStyle& style, float& w, float& h) = 0;
    virtual void play(const std::string& channel, const std::string& spec, double fadein, bool loop) {}
    virtual void queue(const std::string& channel, const std::string& spec, bool loop) {
        if (playing(channel).empty()) play(channel, spec, 0, loop);
    }
    virtual void stop(const std::string& channel, double fadeout) {}
    virtual std::string playing(const std::string& channel) { return {}; }
    virtual double position(const std::string& channel) { return 0; }
    virtual void savePersistent(const std::string& text) {}
    virtual void log(const std::string& line);
};

class Vm {
public:
    Vm(const Assets& assets, const Script& script, Backend& backend);
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;

    // Runs the init code (defines, images, transforms, init python).
    void init(const std::string& persistentText);
    // Starts `label` on the script thread and runs it to its first interaction.
    void start(const std::string& label);
    // Ends the current interaction with `result` (choice index / value) and
    // runs the script to the next one.
    void resume(Value result = {});
    void stop();
    const Interaction& interaction() const { return inter_; }
    bool running() const { return threadAlive_; }
    bool finished() const { return inter_.k == Interaction::K::End || inter_.k == Interaction::K::Restart; }

    // Clock (seconds). Hosts may override it (headless tests).
    std::function<double()> clock;
    double now() const { return clock(); }

    Scene scene;     // the live scene lists
    Scene committed; // the scene as of the last interaction (transition source)

    Interp py;
    std::map<std::string, Value> images; // "bg club_day" -> displayable value
    const Assets& assets;
    const Script& script;
    Backend& backend;
    Value persistent, config, audio, gui, styleNs;
    std::vector<std::string> errors;
    size_t statements = 0;

    // Displayable conversion.
    DispP toDisp(const Value& v);
    DispP imageNamed(const std::vector<std::string>& name);
    Value dispValue(const DispP& d);
    std::shared_ptr<Transition> toTransition(const Value& v);
    TextStyle styleNamed(const std::string& name);

    // Rendering of the master layer in screen coordinates.
    Render renderLayer(const std::vector<Shown>& items, double now);
    // Removes hidden items whose hide handler finished.
    void collectHidden();

    std::string interpolate(const std::string& text);
    std::string savePersistentText();
    double textCps = 50;

private:
    friend struct VmNatives;
    void threadMain();
    Value interact(Interaction in);
    void runFrom(Node* n);
    Node* exec(Node* n);
    void execUser(const std::string& line);
    void execInit(Node* n);
    void say(const Value& who, const std::string& what, const Value& attrs);
    void show(const Value& imspec, const Value& atl, bool scene);
    void hide(const Value& imspec);
    void doWith(const std::string& expr);
    void transition(const std::shared_ptr<Transition>& t);
    void pause(double seconds, bool dismiss);
    Value menu(const std::vector<Interaction::Item>& items, const std::string& caption, const std::string& screen);
    void showDisp(const std::string& tag, const std::vector<std::string>& name, const DispP& disp,
                  const std::vector<Value>& atList, const std::string& behind, int zorder, bool hasZorder);
    void hideTag(const std::string& tag);
    void bindLabel(Node* label, const Value& arguments);
    void installNatives();
    Value callScreen(const std::string& name, Args args, Kwargs kw);

    Value fileObject(const std::string& path, std::string content, bool writable);
    std::string readFile(const std::string& name, bool& ok);

    std::vector<Node*> callStack_;
    Args pendingArgs_;
    Kwargs pendingKw_;
    Interaction inter_;
    Value result_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool vmTurn_ = false, abort_ = false;
    std::atomic<bool> threadAlive_{false};
    struct ThreadHolder;
    std::unique_ptr<ThreadHolder> thread_;
    std::string startLabel_;
    bool windowAuto_ = true;
    std::vector<Interaction::Item> uiButtons_;
    std::vector<std::pair<std::string, Value>> uiTexts_;
};

struct AbortSignal {};
struct RestartSignal {};

} // namespace Ddlc
