#pragma once
// Ren'Py statements loaded from the game's .rpyc files with the control flow
// (Ren'Py's `next` pointers, not pickled) rebuilt.
#include "Ddlc/Archive.hpp"
#include "Ddlc/Value.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Ddlc {

struct Node {
    enum class K {
        Say, Show, Scene, Hide, With, Jump, Call, Return, Pass, Python, EarlyPython, User,
        Menu, If, Label, Define, Default, Image, Transform, Init, Screen, Style, ShowLayer, Other
    } k = K::Other;
    Value ast;                            // the pickled renpy.ast object
    std::vector<std::vector<Node*>> blocks; // Label/Init: [block]; If: per entry; Menu: per item
    Node* next = nullptr;
    std::string file;
    int line = 0;
    const Value& operator[](const char* name) const;
    std::string str(const char* name) const;
};

class Script {
public:
    // Loads every .rpyc of the archives.
    bool load(const Assets& assets, std::string* error = nullptr);
    Node* label(const std::string& name) const;
    bool hasLabel(const std::string& name) const { return labels_.count(name) != 0; }
    // Init-time statements (Define, Default, Image, Transform, Python...) in run order.
    const std::vector<Node*>& inits() const { return inits_; }
    size_t nodeCount() const { return nodes_.size(); }
    size_t fileCount() const { return files_; }

private:
    Node* build(const Value& ast, const std::string& file);
    std::vector<Node*> buildBlock(const Value& list, const std::string& file);
    void chain(std::vector<Node*>& block, Node* after);
    void collect(const std::vector<Node*>& block, int priority, bool init);

    std::vector<std::unique_ptr<Node>> nodes_;
    std::map<std::string, Node*> labels_;
    std::vector<std::pair<int, Node*>> initOrder_;
    std::vector<Node*> inits_;
    size_t files_ = 0, seq_ = 0;
};

} // namespace Ddlc
