#include "DemoStory.hpp"

#include <SDL3/SDL.h>

namespace KotonohaExample {
namespace {
using namespace Kotonoha;
using Story = std::vector<ScriptNode>;

ScriptNode dialogue(int id, std::string name, std::string speaker,
                    std::string text, int next) {
  ScriptNode node{
      id,  NodeType::Dialogue, std::move(speaker), std::move(text), "", "", {},
      next};
  node.name = std::move(name);
  return node;
}

ScriptNode timeline(int id, std::string name, double duration,
                    std::vector<TimelineCue> cues, int next) {
  ScriptNode node{id, TimelineScript{duration, std::move(cues)}, next};
  node.name = std::move(name);
  return node;
}

ScriptNode choice(int id, std::string name, std::string question,
                  std::vector<ChoiceOptionData> options) {
  ScriptNode node{id,
                  NodeType::Choice,
                  "",
                  "",
                  "",
                  "",
                  std::move(question),
                  std::move(options),
                  -1};
  node.name = std::move(name);
  return node;
}

void addOpening(Story &story) {
  TimelineScript opening{
      6,
      {{0, 2, CueType::Subtitle, "SERRA DO ECO - 23:47", ""},
       {2, 4, CueType::Dialogue,
        "A ultima luz do vale desaparece. Uma tempestade se aproxima.",
        "Narrador"}}};
  if (const char *path = SDL_getenv("KOTONOHA_TIMELINE"))
    opening = TimelineScript::load(path);
  if (const char *path = SDL_getenv("KOTONOHA_DEMO_VIDEO"))
    opening.cues.push_back({0, opening.duration, CueType::Video, path, ""});
  if (const char *path = SDL_getenv("KOTONOHA_DEMO_AUDIO"))
    opening.cues.push_back({0, opening.duration, CueType::Audio, path, ""});
  auto node =
      ScriptNode{StoryNode::Opening, std::move(opening), StoryNode::Camp};
  node.name = "Opening / timed";
  story.push_back(std::move(node));
}

void addCamp(Story &story, DemoState &state,
             std::function<void()> requestName) {
  story.push_back(dialogue(
      0, "Camp / arrival", "Narrador",
      "As sombras se esgueiravam pela entrada da caverna misteriosa.", 1));
  story.push_back(dialogue(1, "Camp / companion", "Companheiro",
                           "Espere! Antes de entrarmos... como devo te chamar?",
                           2));
  ScriptNode name{2, NodeType::Action, std::move(requestName), -1};
  name.name = "Camp / player name";
  name.text = "Digite o seu nome:";
  story.push_back(std::move(name));
  story.push_back(dialogue(
      3, "Camp / preparation", "Companheiro",
      "Certo, {player}! Precisamos nos preparar antes da travessia.", 4));
  story.push_back(
      choice(4, "Camp / equipment", "O que voce decide pegar no acampamento?",
             {{"Pegar a Tocha Antiga", 5}, {"Pegar o Mapa Desenhado", 6}}));
  ScriptNode torch{5, NodeType::Action, [&state] { state.hasTorch = true; }, 7};
  torch.name = "Inventory / torch";
  story.push_back(std::move(torch));
  ScriptNode map{6, NodeType::Action, [&state] { state.hasMap = true; }, 8};
  map.name = "Inventory / map";
  story.push_back(std::move(map));
  story.push_back(dialogue(
      7, "Camp / torch", "Narrador",
      "Voce acende a tocha. O fogo afasta a escuridao densa do local.", 30));
  story.push_back(dialogue(
      8, "Camp / map", "Narrador",
      "Voce desenrola o antigo mapa e identifica os atalhos seguros.", 30));
}

void addStorm(Story &story) {
  TimelineCue question{4, 3, CueType::Choice,
                       "O vento esconde o sinal. O que fazer?", ""};
  question.choices = {{"Esperar o sinal do companheiro", 31},
                      {"Continuar com cuidado", 32}};
  // The default Pause policy deliberately waits at the deadline, not at entry.
  story.push_back(
      timeline(30, "Storm / timed, pause on unanswered choice", 7,
               {{0, 2, CueType::Subtitle, "[O vento aumenta]", ""},
                {1, 3, CueType::Dialogue,
                 "A trilha esta sumindo na chuva. Fique perto!", "Companheiro"},
                question},
               10));
  story.push_back(dialogue(31, "Storm / regroup", "Companheiro",
                           "Boa ideia. Vamos escolher uma rota juntos.", 10));
  story.push_back(
      dialogue(32, "Storm / advance", "Narrador",
               "Voce marca o caminho nas pedras e chama seu companheiro.", 10));
  story.push_back(choice(10, "Crossroads / traditional",
                         "Qual rota deseja escolher?",
                         {{"Seguir pela Caverna Escura", 11},
                          {"Subir pela Trilha da Montanha", 12}}));
}

void addRoutes(Story &story, DemoState &state) {
  story.push_back(
      timeline(11, "Cave / timed", 5,
               {{0, 2, CueType::Subtitle, "[Gotas ecoam no tunel]", ""},
                {1, 4, CueType::Dialogue,
                 "Voces entram no tunel subterraneo gelado.", "Narrador"}},
               13));
  ScriptNode cave{13, NodeType::Conditional,
                  [&state] { return state.hasTorch ? 14 : 15; }};
  cave.name = "Cave / equipment check";
  story.push_back(std::move(cave));
  story.push_back(dialogue(
      14, "Cave / illuminated", "Narrador",
      "Gracas a tocha, voces evitam um abismo oculto e encontram a saida!",
      40));
  story.push_back(dialogue(
      15, "Cave / darkness", "Companheiro",
      "Nao temos luz. Segure minha mao: ainda podemos atravessar juntos.", 40));
  story.push_back(timeline(
      12, "Mountain / timed", 5,
      {{0, 2, CueType::Subtitle, "[Trovoes sobre a serra]", ""},
       {1, 4, CueType::Dialogue,
        "O vento corta a encosta. A ponte esta adiante!", "Companheiro"}},
      16));
  ScriptNode mountain{16, NodeType::Conditional,
                      [&state] { return state.hasMap ? 17 : 18; }};
  mountain.name = "Mountain / equipment check";
  story.push_back(std::move(mountain));
  story.push_back(
      dialogue(17, "Mountain / mapped route", "Narrador",
               "O mapa revela um atalho ate a ponte antes da tempestade.", 40));
  story.push_back(dialogue(
      18, "Mountain / lost route", "Companheiro",
      "Erramos a trilha, mas vejo a ponte entre as arvores. Vamos!", 40));
}

void addEscape(Story &story, DemoState &state) {
  TimelineCue question{0, 4, CueType::Choice,
                       "A ponte cede! Decida antes do proximo trovao.", ""};
  question.choices = {{"Ajudar o companheiro a atravessar", 41},
                      {"Correr sozinho para a margem", 42}};
  question.timeout = ChoiceTimeout::Select;
  question.timeoutTargetNodeId = 42;
  story.push_back(timeline(40, "Bridge / timed, default route on timeout", 4,
                           {{0, 4, CueType::Subtitle,
                             "[A madeira estala - voce tem 4 segundos]", ""},
                            question},
                           42));
  ScriptNode help{41, NodeType::Action, [&state] { state.trustLevel = 1; }, 43};
  help.name = "Bridge / help companion";
  story.push_back(std::move(help));
  ScriptNode run{42, NodeType::Action, [&state] { state.trustLevel = -1; }, 44};
  run.name = "Bridge / run alone";
  story.push_back(std::move(run));
  story.push_back(dialogue(43, "Bridge / together", "Companheiro",
                           "Voce voltou por mim. Agora ninguem fica para tras!",
                           50));
  story.push_back(
      dialogue(44, "Bridge / separated", "Narrador",
               "A chuva engole a voz do seu companheiro do outro lado.", 50));
  ScriptNode ending{50, NodeType::Conditional,
                    [&state] { return state.trustLevel > 0 ? 20 : 21; }};
  ending.name = "Ending / trust check";
  story.push_back(std::move(ending));
  story.push_back(dialogue(
      20, "Ending / together", "Narrador",
      "[BOM FIM] Parabens {player}, voces alcancam o abrigo juntos!", -1));
  story.push_back(dialogue(21, "Ending / alone", "Narrador",
                           "[FIM SOLITARIO] {player}, a tempestade passou. Mas "
                           "alguem ficou para tras.",
                           -1));
}
} // namespace

std::vector<Kotonoha::ScriptNode>
buildDemoStory(DemoState &state, std::function<void()> requestName) {
  Story story;
  addOpening(story);
  addCamp(story, state, std::move(requestName));
  addStorm(story);
  addRoutes(story, state);
  addEscape(story, state);
  return story;
}

} // namespace KotonohaExample
