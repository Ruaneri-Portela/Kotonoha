#include <Kotonoha/routing/SchoolDaysReplayPlaybackSession.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using Session = Kotonoha::SchoolDaysReplayPlaybackSession;

struct FakeGameplayStorage {
    int serial = 0;
};

struct Fixture {
    int creates = 0;
    int destroys = 0;
    int live = 0;
    int maxLive = 0;
    int resolveCalls = 0;
    int failResolveAt = -1;
    int failCreateAt = -1;
};

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "SCHOOL DAYS REPLAY PLAYBACK SESSION GATE FAIL: " << message << '\n';
    std::exit(2);
}

void Check(bool value, const char* message) {
    if (!value) Fail(message);
}

bool Resolve(const char* sceneKey, Kotonoha::SchoolDaysKtrfScene* scene,
             void* userdata, Kotonoha_KtrfError* error) {
    auto* fixture = static_cast<Fixture*>(userdata);
    const int call = fixture->resolveCalls++;
    if (call == fixture->failResolveAt) {
        if (error != nullptr) error->code = KOTONOHA_KTRF_ERROR_IO;
        return false;
    }
    scene->sceneKey = sceneKey;
    scene->orsPath = std::string("fixture/") + sceneKey + ".ENG.ORS";
    if (error != nullptr) error->code = KOTONOHA_KTRF_OK;
    return true;
}

Kotonoha::Gameplay* Create(const char*, Kotonoha_Game*, void* userdata) {
    auto* fixture = static_cast<Fixture*>(userdata);
    const int call = fixture->creates;
    if (call == fixture->failCreateAt) {
        ++fixture->creates;
        return nullptr;
    }
    auto* storage = new FakeGameplayStorage{};
    storage->serial = ++fixture->creates;
    ++fixture->live;
    if (fixture->live > fixture->maxLive) fixture->maxLive = fixture->live;
    return reinterpret_cast<Kotonoha::Gameplay*>(storage);
}

void Destroy(Kotonoha::Gameplay* gameplay, void* userdata) {
    auto* fixture = static_cast<Fixture*>(userdata);
    delete reinterpret_cast<FakeGameplayStorage*>(gameplay);
    ++fixture->destroys;
    --fixture->live;
}

Session::Resolver ResolverFor(Fixture* fixture) {
    return {&Resolve, fixture};
}

Kotonoha::SchoolDaysKtrfGameplayBridge::Factory FactoryFor(Fixture* fixture) {
    Kotonoha::SchoolDaysKtrfGameplayBridge::Factory factory;
    factory.create = &Create;
    factory.destroy = &Destroy;
    factory.userdata = fixture;
    return factory;
}

void RunAllOrdinarySequences() {
    for (int index = 0; index < Kotonoha::SchoolDaysReplayScriptOracle::HSceneCount();
         ++index) {
        if (Kotonoha::SchoolDaysReplayScriptOracle::CursorResolutionFor(index) !=
            Kotonoha::SchoolDaysReplayScriptOracle::TransitionCursorResolution::StaticTableOrder) {
            continue;
        }
        Fixture fixture;
        int gameStub = 0;
        auto* game = reinterpret_cast<Kotonoha_Game*>(&gameStub);
        Session session;
        Kotonoha_KtrfError error{};
        Check(session.StartOrdinary(index, game, ResolverFor(&fixture),
                                    FactoryFor(&fixture), &error),
              "ordinary HScene start failed");
        const std::size_t expected = session.SequenceCount();
        Check(expected > 0u && session.SequenceIndex() == 0u &&
                  session.CurrentGameplay() != nullptr,
              "ordinary HScene initial ownership drifted");
        bool completed = false;
        while (!completed) {
            Check(session.AdvanceAfterSceneEnd(&completed, &error),
                  "ordinary HScene advance failed");
        }
        Check(!session.IsOpen() && session.CurrentGameplay() == nullptr &&
                  fixture.creates == static_cast<int>(expected) &&
                  fixture.destroys == static_cast<int>(expected) &&
                  fixture.live == 0 && fixture.maxLive <= 2,
              "ordinary HScene ownership/count drifted");
    }
}

void CheckSpecialPlayback() {
    Fixture fixture;
    int gameStub = 0;
    auto* game = reinterpret_cast<Kotonoha_Game*>(&gameStub);
    Session session;
    Kotonoha_KtrfError error{};
    Check(!session.StartOrdinary(11, game, ResolverFor(&fixture),
                                 FactoryFor(&fixture), &error),
          "special SceneCheck index was flattened into ordinary playback");
    Check(fixture.creates == 0 && fixture.destroys == 0 && fixture.live == 0,
          "special SceneCheck rejection changed ownership");

    Check(session.StartSpecialChoice(11, 0, game, ResolverFor(&fixture),
                                     FactoryFor(&fixture), &error),
          "special HScene11 playback start failed");
    Check(session.IsSpecialChoicePlayback() && session.SpecialChoice() == 0 &&
              session.SequenceIndex() == 0u &&
              session.CurrentScene().sceneKey == "03/03-KB-N00",
          "special HScene11 initial cursor drifted");
    bool completed = false;
    Check(session.AdvanceAfterSceneEnd(0, &completed, &error) && !completed &&
              session.SequenceIndex() == 1u &&
              session.CurrentScene().sceneKey == "03/03-KB-N02",
          "special HScene11 select0 cursor transition drifted");
    Check(session.AdvanceAfterSceneEnd(-2, &completed, &error) && !completed &&
              session.SequenceIndex() == 6u &&
              session.CurrentScene().sceneKey == "03/03-KB-N07",
          "special HScene11 no-select cursor transition drifted");
    session.Close();
    Check(fixture.live == 0, "special HScene11 close leaked Gameplay");

    Fixture restartFixture;
    Session restart;
    Check(restart.StartSpecialChoice(11, 0, game, ResolverFor(&restartFixture),
                                     FactoryFor(&restartFixture), &error),
          "special HScene11 restart fixture start failed");
    completed = false;
    Check(restart.AdvanceAfterSceneEnd(1, &completed, &error) && !completed &&
              restart.SequenceIndex() == 2u &&
          restart.AdvanceAfterSceneEnd(0, &completed, &error) && !completed &&
              restart.SequenceIndex() == 0u &&
              restart.CurrentScene().sceneKey == "03/03-KB-N00",
          "special HScene11 valid cursor0 restart drifted");
    restart.Close();
    Check(restartFixture.live == 0,
          "special HScene11 restart fixture leaked Gameplay");

    Fixture sequentialFixture;
    Session sequential;
    Check(sequential.StartSpecialChoice(22, 1, game,
                                        ResolverFor(&sequentialFixture),
                                        FactoryFor(&sequentialFixture), &error),
          "special HScene22 playback start failed");
    completed = false;
    Check(sequential.AdvanceAfterSceneEnd(1, &completed, &error) && !completed &&
              sequential.SequenceIndex() == 1u &&
          sequential.AdvanceAfterSceneEnd(-2, &completed, &error) && !completed &&
              sequential.SequenceIndex() == 2u &&
          sequential.AdvanceAfterSceneEnd(0, &completed, &error) && completed &&
              !sequential.IsOpen(),
          "special HScene22 sequential playback drifted");
    Check(sequentialFixture.live == 0 && sequentialFixture.maxLive <= 2,
          "special HScene22 ownership drifted");

    Fixture invalidResultFixture;
    Session invalidResult;
    Check(invalidResult.StartSpecialChoice(30, 0, game,
                                           ResolverFor(&invalidResultFixture),
                                           FactoryFor(&invalidResultFixture), &error),
          "special HScene30 invalid-result fixture start failed");
    Kotonoha::Gameplay* const beforeGameplay = invalidResult.CurrentGameplay();
    const std::string beforeScene = invalidResult.CurrentScene().sceneKey;
    completed = false;
    Check(!invalidResult.AdvanceAfterSceneEnd(2, &completed, &error),
          "special HScene30 accepted unsupported select result");
    Check(!completed && invalidResult.IsOpen() &&
              invalidResult.SequenceIndex() == 0u &&
              invalidResult.CurrentGameplay() == beforeGameplay &&
              invalidResult.CurrentScene().sceneKey == beforeScene &&
              invalidResultFixture.live == 1 && invalidResultFixture.destroys == 0,
          "special invalid-result failure mutated replay ownership");
    invalidResult.Close();
    Check(invalidResultFixture.live == 0 && invalidResultFixture.destroys == 1,
          "special invalid-result fixture close leaked Gameplay");
}

void CheckTransactionalNextCreateFailure() {
    Fixture fixture;
    int gameStub = 0;
    auto* game = reinterpret_cast<Kotonoha_Game*>(&gameStub);
    Session session;
    Kotonoha_KtrfError error{};
    Check(session.StartOrdinary(1, game, ResolverFor(&fixture),
                                FactoryFor(&fixture), &error),
          "rollback fixture start failed");
    Kotonoha::Gameplay* const beforeGameplay = session.CurrentGameplay();
    const std::string beforeScene = session.CurrentScene().sceneKey;
    fixture.failCreateAt = fixture.creates;
    bool completed = false;
    Check(!session.AdvanceAfterSceneEnd(&completed, &error),
          "forced next Gameplay create failure unexpectedly succeeded");
    Check(!completed && session.IsOpen() && session.SequenceIndex() == 0u &&
              session.CurrentGameplay() == beforeGameplay &&
              session.CurrentScene().sceneKey == beforeScene &&
              fixture.live == 1 && fixture.destroys == 0,
          "failed next Gameplay creation did not preserve outgoing replay scene");
    session.Close();
    Check(fixture.live == 0 && fixture.destroys == 1,
          "rollback fixture close leaked outgoing replay Gameplay");
}

} // namespace

int main() {
    RunAllOrdinarySequences();
    CheckSpecialPlayback();
    CheckTransactionalNextCreateFailure();
    std::cout << "SCHOOL DAYS REPLAY PLAYBACK SESSION GATE PASS\n"
              << "ordinary_hscene_sequences=PASS\n"
              << "temporary_gameplay_ownership=PASS\n"
              << "special_scenecheck_native_cursor=PASS\n"
              << "next_create_failure_atomic=PASS\n";
    return 0;
}
