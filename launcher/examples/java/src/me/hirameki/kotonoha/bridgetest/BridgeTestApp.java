package me.hirameki.kotonoha.bridgetest;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalTime;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Supplier;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.util.Duration;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.value.ChangeListener;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.TableCell;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableRow;
import javafx.scene.control.TableView;
import javafx.scene.control.TextArea;
import javafx.scene.control.TextField;
import javafx.scene.control.TitledPane;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.BorderPane;
import javafx.scene.layout.GridPane;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import me.hirameki.kotonoha.bridgetest.JsonUtil.GameInfo;
import me.hirameki.kotonoha.launcher.KotonohaLauncherBridge;

/**
 * JavaFX UI for the KotonohaLauncherCore JNI bridge. The scan table lists every
 * discovered game and "Launch game" starts only the selected row, one game at
 * a time, with the full KotonohaGameLaunchRequest assembled from the scan JSON.
 */
public final class BridgeTestApp extends Application {
    private static final String SERVER_ROOT = "/Volumes/Server/data2/Games";
    private static final String EXTERNAL_ROOT = "/Volumes/Nero 2TB/KotonohaAssets";
    private static boolean attachedMode;
    private Stage primaryStage;

    private static final String GREEN_ROW =
            "-fx-background-color: #b9f6c6; -fx-text-fill: #0b5a20;";
    private static final String GREEN_CELL =
            "-fx-background-color: #b9f6c6; -fx-text-fill: #0b5a20; "
            + "-fx-font-weight: bold;";

    private final ObservableList<String> roots = FXCollections.observableArrayList();
    private final TableView<GameRow> gamesTable = new TableView<>();
    private final TextArea outputArea = new TextArea();
    private final TextArea logArea = new TextArea();
    private final TextArea gameLogArea = new TextArea();
    // Log de cada jogo, indexado pelo caminho do scan (sobrevive a re-scans).
    private final Map<String, StringBuilder> gameLogs = new LinkedHashMap<>();
    private static final class HostState {
        final GameRow row;
        final Path logPath;
        long offset;
        HostState(GameRow row, Path logPath) { this.row = row; this.logPath = logPath; }
    }
    private final Map<Long, HostState> hosts = new LinkedHashMap<>();
    private Timeline hostPoll;
    private final TextField newRootField = new TextField();
    private final TextField detectField = new TextField();
    private final TextField profileField = new TextField("bridge-test-profile");
    private final TextField ktrfField = new TextField();
    private final TextField saveRootField = new TextField();
    private final CheckBox splitBox = new CheckBox("Split saves by game");
    private final CheckBox dryRunBox = new CheckBox("Test launcher only");
    private final Label statusLabel = new Label("Pronto.");

    // Todos os parametros opcionais da request; preenchidos a partir do JSON.
    private final TextField overlayField = new TextField();
    private final TextField translationField = new TextField();
    private final TextField subtitleField = new TextField();
    private final TextField documentsField = new TextField();
    private final TextField routeField = new TextField();
    private final TextField scriptsField = new TextField();
    private final TextField startField = new TextField();
    private final TextField rendererField = new TextField();
    private final TextField upscalerField = new TextField();
    private final TextField presetField = new TextField();
    private final TextField factorField = new TextField();

    private ListView<String> rootsList;

    @Override
    public void start(Stage stage) {
        primaryStage = stage;
        boolean loaded = KotonohaLauncherBridge.ensureLoaded();
        if (loaded) {
            log("JNI carregado diretamente do core: OK");
        } else {
            log("FALHA ao carregar JNI: " + KotonohaLauncherBridge.getLoadError());
        }

        String defaultRoot = new File(SERVER_ROOT).exists() ? SERVER_ROOT
                : new File(EXTERNAL_ROOT).exists() ? EXTERNAL_ROOT : "";
        if (!defaultRoot.isEmpty()) {
            roots.add(defaultRoot);
            detectField.setText(defaultRoot);
            ktrfField.setText(defaultRoot);
        }
        newRootField.setText(defaultRoot);

        stage.setTitle("Kotonoha Launcher — jogos individuais (JavaFX)");
        stage.setScene(new Scene(buildRoot(), 1460, 900));
        stage.show();
        log("Pronto. Scan -> selecione um jogo -> Launch game (um por vez).");

        // Modo de verificacao automatica (sem automacao de mouse/Accessibility).
        List<String> raw = getParameters().getRaw();
        String autoLaunch = null;
        boolean validate = raw.contains("--validate");
        for (String arg : raw) {
            if (arg.startsWith("--auto-launch=")) {
                autoLaunch = arg.substring("--auto-launch=".length());
            }
        }
        if (raw.contains("--auto-scan") || autoLaunch != null) {
            log("--auto-scan: disparando Scan pela mesma logica do botao");
            run("Scan", () -> KotonohaLauncherBridge.nativeScanRootsJson(
                    roots.toArray(new String[0]), splitBox.isSelected(),
                    nullToEmpty(saveRootField.getText())));
        }
        if (autoLaunch != null) {
            GameRow found = findGame(autoLaunch);
            if (found == null) {
                log("--auto-launch: jogo nao encontrado: " + autoLaunch);
                status("Jogo nao encontrado: " + autoLaunch);
            } else {
                gamesTable.getSelectionModel().select(found);
                gamesTable.scrollTo(found);
                fillOptionalFields(found.info);
                showGameLog(found);
                log("--auto-launch: selecionado " + found.title.get()
                        + " (" + found.path.get() + ")");
                launchSelected(validate);
            }
        }
    }

    private BorderPane buildRoot() {
        BorderPane root = new BorderPane();
        root.setTop(buildTop());
        root.setLeft(buildLeft());   // controles / opcoes a esquerda
        root.setRight(buildRight()); // lista de jogos a direita + log do jogo
        root.setBottom(buildLog());
        return root;
    }

    private VBox buildTop() {
        Label title = new Label("KotonohaLauncherCore — ponte JNI / launch individual");
        title.setStyle("-fx-font-size: 16px; -fx-font-weight: bold;");
        VBox top = new VBox(4, title, statusLabel);
        top.setPadding(new Insets(10, 10, 4, 10));
        return top;
    }

    /** Coluna esquerda: roots, opcoes, acoes, parametros e saida JSON. */
    private VBox buildLeft() {
        Label libraries = new Label("Bibliotecas / pastas de jogos");
        libraries.setStyle("-fx-font-weight: bold;");

        Button addButton = new Button("Add folder");
        addButton.setOnAction(e -> addRoot(newRootField.getText()));
        addButton.setTooltip(new Tooltip("Adiciona uma pasta/biblioteca à lista de roots do scan"));
        newRootField.setOnAction(e -> addRoot(newRootField.getText()));

        Button removeButton = new Button("Remove");
        removeButton.setOnAction(e -> {
            String selected = rootsList.getSelectionModel().getSelectedItem();
            if (selected != null) roots.remove(selected);
        });

        HBox addRow = new HBox(8, new Label("Root:"), newRootField, addButton, removeButton);
        addRow.setAlignment(Pos.CENTER_LEFT);
        HBox.setHgrow(newRootField, Priority.ALWAYS);

        rootsList = new ListView<>(roots);
        rootsList.getSelectionModel().setSelectionMode(SelectionMode.SINGLE);
        rootsList.setPrefHeight(62);

        HBox options = new HBox(10, splitBox, dryRunBox,
                new Label("Custom save root:"), saveRootField);
        options.setAlignment(Pos.CENTER_LEFT);
        HBox.setHgrow(saveRootField, Priority.ALWAYS);

        TitledPane paramsPane = new TitledPane(
                "Parâmetros opcionais do launch (da request / do JSON de scan)",
                buildOptionalFields());
        paramsPane.setExpanded(false);

        outputArea.setEditable(false);
        outputArea.setPromptText("JSON formatado");
        outputArea.setStyle("-fx-font-family: 'Menlo'; -fx-font-size: 11px;");
        TitledPane outputPane = new TitledPane("Saída JSON", outputArea);
        VBox.setVgrow(outputPane, Priority.ALWAYS);

        VBox left = new VBox(6, libraries, addRow, rootsList, options,
                buildActions(), paramsPane, outputPane);
        left.setPadding(new Insets(4, 6, 10, 10));
        left.setPrefWidth(560);
        left.setMinWidth(520);
        return left;
    }

    private TitledPane buildActions() {
        Button scan = button("Scan", () -> KotonohaLauncherBridge.nativeScanRootsJson(
                roots.toArray(new String[0]), splitBox.isSelected(),
                nullToEmpty(saveRootField.getText())));
        scan.setTooltip(new Tooltip("nativeScanRootsJson(String[], boolean, String)"));

        Button detect = button("Detect", () -> KotonohaLauncherBridge.nativeDetectPathJson(
                nullToEmpty(detectField.getText()), splitBox.isSelected(),
                nullToEmpty(saveRootField.getText())));
        detect.setTooltip(new Tooltip("nativeDetectPathJson(path, split, saveRoot)"));

        Button loadParams = button("Params: load", () -> KotonohaLauncherBridge
                .nativeLoadGameParamsJson(nullToEmpty(profileField.getText())));
        loadParams.setTooltip(new Tooltip("nativeLoadGameParamsJson(saveProfileId)"));

        Button saveParams = new Button("Params: save");
        saveParams.setTooltip(new Tooltip("nativeSaveGameParams(...)"));
        saveParams.setOnAction(e -> {
            try {
                String profile = nullToEmpty(profileField.getText());
                String firstRoot = roots.isEmpty() ? "" : roots.get(0);
                boolean ok = KotonohaLauncherBridge.nativeSaveGameParams(
                        profile, "", "", "", "", ktrfField.getText(), "", "", "", "", "", "");
                log("nativeSaveGameParams -> " + ok);
                showText("{\"saved\":" + ok + ",\"profile\":\"" + profile
                        + "\",\"ktrfPath\":\"" + ktrfField.getText()
                        + "\",\"gamePath\":\"" + firstRoot + "\"}");
            } catch (Throwable t) {
                fail("Params: save", t);
            }
        });

        Button presets = button("Upscale presets",
                KotonohaLauncherBridge::nativeUpscalePresetsJson);
        Button defaultRoute = button("Default route",
                KotonohaLauncherBridge::nativeDefaultSchoolDaysRoute);

        Button validate = button("Validate route", () -> KotonohaLauncherBridge
                .nativeValidateRoute(nullToEmpty(ktrfField.getText())));

        Button resolveSave = button("Resolve save dir", () -> KotonohaLauncherBridge
                .nativeResolveSaveDirectory(
                        nullToEmpty(detectField.getText()), "school-days",
                        nullToEmpty(profileField.getText()), splitBox.isSelected(),
                        nullToEmpty(saveRootField.getText())));

        Button launchOne = new Button("Launch game");
        launchOne.setStyle("-fx-base: #356635; -fx-text-fill: white;");
        launchOne.setTooltip(new Tooltip(
                "Inicia somente o jogo selecionado na tabela (request completo)"));
        launchOne.setOnAction(e -> launchSelected(false));

        Button validateRequest = new Button("Validate request");
        validateRequest.setTooltip(new Tooltip(
                "Monta e confirma a request sem abrir SDL/app"));
        validateRequest.setOnAction(e -> launchSelected(true));

        HBox first = new HBox(8, scan, detect, presets, defaultRoute, resolveSave);
        HBox second = new HBox(8, loadParams, saveParams, validate,
                new Label("Profile:"), profileField,
                new Label("Path / .ktrf:"), detectField, ktrfField);
        second.setAlignment(Pos.CENTER_LEFT);
        HBox.setHgrow(ktrfField, Priority.ALWAYS);
        ktrfField.setPrefWidth(240);

        HBox third = new HBox(8, launchOne, validateRequest);
        third.setAlignment(Pos.CENTER_LEFT);

        TitledPane pane = new TitledPane("Ações da ponte JNI (chamadas nativas reais)",
                new VBox(6, first, new HBox(8, new Label("Detect path:"), detectField),
                        second, third));
        pane.setExpanded(true);
        return pane;
    }

    private Button button(String text, Supplier<String> call) {
        Button b = new Button(text);
        b.setOnAction(e -> run(text, call));
        return b;
    }

    /** Coluna direita: lista de jogos (verde quando launchado) + log do jogo. */
    private VBox buildRight() {
        TableColumn<GameRow, String> colTitle = new TableColumn<>("Título");
        colTitle.setCellValueFactory(c -> c.getValue().title);
        TableColumn<GameRow, String> colEngine = new TableColumn<>("Engine");
        colEngine.setCellValueFactory(c -> c.getValue().engine);
        TableColumn<GameRow, String> colProfile = new TableColumn<>("Save profile");
        colProfile.setCellValueFactory(c -> c.getValue().saveProfileId);
        TableColumn<GameRow, Boolean> colState = new TableColumn<>("Estado");
        colState.setCellValueFactory(c -> c.getValue().running);
        colState.setCellFactory(col -> new TableCell<GameRow, Boolean>() {
            @Override
            protected void updateItem(Boolean item, boolean empty) {
                super.updateItem(item, empty);
                if (empty) {
                    setText(null);
                    setStyle("");
                    return;
                }
                boolean running = Boolean.TRUE.equals(item);
                setText(running ? "● RUNNING" : "—");
                setStyle(running ? GREEN_CELL : "");
            }
        });
        TableColumn<GameRow, String> colPath = new TableColumn<>("Caminho");
        colPath.setCellValueFactory(c -> c.getValue().path);
        gamesTable.getColumns().addAll(colTitle, colEngine, colProfile, colState, colPath);
        gamesTable.setPlaceholder(new Label("Faça Scan para listar os jogos detectados"));
        gamesTable.getSelectionModel().setSelectionMode(SelectionMode.SINGLE);
        gamesTable.getSelectionModel().selectedItemProperty().addListener(
                (obs, oldRow, row) -> {
                    if (row != null) fillOptionalFields(row.info);
                    // Clicar em um jogo mostra o log daquele launch/engine.
                    showGameLog(row);
                });
        gamesTable.setRowFactory(tv -> new TableRow<GameRow>() {
            private final ChangeListener<Boolean> launchListener =
                    (obs, was, is) -> setStyle(is ? GREEN_ROW : "");

            @Override
            protected void updateItem(GameRow item, boolean empty) {
                GameRow previous = getItem();
                if (previous != null) previous.running.removeListener(launchListener);
                super.updateItem(item, empty);
                if (item != null && !empty) {
                    item.running.addListener(launchListener);
                    setStyle(item.running.get() ? GREEN_ROW : "");
                } else {
                    setStyle("");
                }
            }
        });
        colTitle.setPrefWidth(150);
        colEngine.setPrefWidth(125);
        colProfile.setPrefWidth(110);
        colState.setPrefWidth(105);
        colPath.setPrefWidth(330);

        gameLogArea.setEditable(false);
        gameLogArea.setPromptText("Selecione um jogo para ver o log do launch/engine");
        gameLogArea.setStyle("-fx-font-family: 'Menlo'; -fx-font-size: 11px;");
        showGameLog(null);

        TitledPane tablePane = new TitledPane(
                "Jogos detectados (selecione UM e clique em Launch game)", gamesTable);
        VBox.setVgrow(tablePane, Priority.ALWAYS);
        TitledPane gameLogPane = new TitledPane(
                "Log do jogo selecionado (launch / engine)", gameLogArea);
        VBox.setVgrow(gameLogPane, Priority.ALWAYS);
        VBox right = new VBox(6, tablePane, gameLogPane);
        right.setPadding(new Insets(4, 10, 10, 6));
        right.setPrefWidth(830);
        right.setMinWidth(700);
        return right;
    }

    private GridPane buildOptionalFields() {
        GridPane grid = new GridPane();
        grid.setHgap(8);
        grid.setVgap(5);
        grid.setPadding(new Insets(8));
        addField(grid, 0, 0, "Asset overlay", overlayField, 300);
        addField(grid, 2, 0, "Tradução (.ktr)", translationField, 300);
        addField(grid, 0, 1, "Estilo legenda", subtitleField, 300);
        addField(grid, 2, 1, "Documents dir", documentsField, 300);
        addField(grid, 0, 2, "KTRF / rota", routeField, 300);
        addField(grid, 2, 2, "Scripts dir", scriptsField, 300);
        addField(grid, 0, 3, "Cenário inicial", startField, 300);
        addField(grid, 2, 3, "Renderer", rendererField, 300);
        addField(grid, 0, 4, "Upscaler", upscalerField, 300);
        addField(grid, 2, 4, "Preset", presetField, 300);
        addField(grid, 0, 5, "Fator", factorField, 100);
        return grid;
    }

    private static void addField(GridPane grid, int col, int row, String label,
                                 TextField field, int width) {
        grid.add(new Label(label), col, row);
        field.setPrefWidth(width);
        grid.add(field, col + 1, row);
    }

    private VBox buildLog() {
        logArea.setEditable(false);
        logArea.setPrefHeight(110);
        logArea.setStyle("-fx-font-family: 'Menlo'; -fx-font-size: 11px;");
        return new VBox(new TitledPane("Log geral da ponte", logArea));
    }

    private void addRoot(String path) {
        String value = path == null ? "" : path.trim();
        if (value.isEmpty()) return;
        if (!roots.contains(value)) roots.add(value);
        log("Root adicionado: " + value);
    }

    private void run(String name, Supplier<String> call) {
        try {
            log(name + " ...");
            String result = call.get();
            log(name + " OK (" + (result == null ? 0 : result.length()) + " chars)");
            if ("Scan".equals(name)) show(result);
            else showText(JsonUtil.pretty(result));
        } catch (Throwable t) {
            fail(name, t);
        }
    }

    private void fail(String name, Throwable t) {
        log(name + " ERRO: " + t);
        status(name + " ERRO");
        outputArea.setText(String.valueOf(t));
    }

    private void show(String json) {
        showText(JsonUtil.pretty(json));
        List<GameInfo> parsed = JsonUtil.gameInfos(json);
        gamesTable.getItems().setAll(parsed.stream().map(game -> {
            GameRow g = new GameRow();
            g.info = game;
            g.running.set(false);
            g.title.set(game.title.isEmpty() ? "(sem título)" : game.title);
            g.engine.set(game.engineSlug.isEmpty() ? "(desconhecido)" : game.engineSlug);
            g.saveProfileId.set(game.saveProfileId);
            g.path.set(game.path);
            return g;
        }).toList());
        log("Scan/JSON: " + parsed.size() + " jogo(s) na tabela");
    }

    private void showText(String text) {
        outputArea.setText(text);
    }

    private GameRow findGame(String query) {
        String needle = query.toLowerCase();
        for (GameRow row : gamesTable.getItems()) {
            if (row.path.get().toLowerCase().contains(needle)
                    || row.title.get().toLowerCase().contains(needle)
                    || row.engine.get().toLowerCase().contains(needle)) {
                return row;
            }
        }
        return null;
    }

    private void fillOptionalFields(GameInfo game) {
        overlayField.setText(game.params.getOrDefault("assetOverlayPath",
                game.overlayPath));
        translationField.setText(game.params.getOrDefault("translationPath", ""));
        subtitleField.setText(game.params.getOrDefault("subtitleStylePath", ""));
        documentsField.setText(game.params.getOrDefault("customDocumentsDir", ""));
        ktrfField.setText(game.params.getOrDefault("ktrfPath", ""));
        routeField.setText(game.params.getOrDefault("ktrfPath", ""));
        scriptsField.setText(game.params.getOrDefault("scriptsDir", ""));
        startField.setText(game.params.getOrDefault("startScenario", ""));
        rendererField.setText(game.params.getOrDefault("rendererBackend", ""));
        upscalerField.setText(game.params.getOrDefault("upscalerBackend", ""));
        presetField.setText(game.params.getOrDefault("upscalerPreset", ""));
        factorField.setText(game.params.getOrDefault("upscalerFactor", ""));
    }

    /** Launch exactly the selected game, with all fields from the scan JSON. */
    private void launchSelected(boolean validate) {
        GameRow row = gamesTable.getSelectionModel().getSelectedItem();
        if (row == null || row.path.get().isEmpty()) {
            log("Launch game: nenhum jogo selecionado");
            status("Nenhum jogo selecionado");
            return;
        }
        LaunchRequest request = LaunchRequest.from(row.info)
                .split(splitBox.isSelected())
                .saveRoot(saveRootField.getText())
                .overlay(overlayField.getText())
                .translation(translationField.getText())
                .subtitle(subtitleField.getText())
                .documents(documentsField.getText())
                .ktrf(routeField.getText())
                .scripts(scriptsField.getText())
                .start(startField.getText())
                .renderer(rendererField.getText())
                .upscaler(upscalerField.getText())
                .preset(presetField.getText())
                .factor(factorField.getText())
                .dryRun(dryRunBox.isSelected())
                .build();
        if (validate) {
            logFor(row, "Validate request => launch request validated; "
                    + "graphical launch requires SDL app");
            logFor(row, "REQUEST " + request.describe().replace('\n', ' '));
            showText(request.describe());
            showGameLog(row);
            status("Request validada: " + row.title.get());
            return;
        }
        if (attachedMode) {
            primaryStage.hide();
            try {
                String attached = request.launch();
                logFor(row, "ATTACHED " + attached);
                showText(attached);
            } finally {
                primaryStage.show();
            }
            return;
        }
        if (hosts.values().stream().anyMatch(host -> host.row == row)) {
            log(row.title.get() + " ja esta em execucao");
            return;
        }
        // Cada jogo tem seu proprio host; varios podem rodar ao mesmo tempo.
        row.running.set(true);
        logFor(row, "Launch game: " + row.path.get() + " engine="
                + request.engineSlug + " profile=" + request.saveProfileId
                + " dryRun=" + request.dryRun);
        status(request.dryRun ? "Validando request..."
                : "Aguardando jogo... feche a janela para voltar");
        // O runLater permite um pulso do FX thread para pintar a linha verde
        // antes da chamada JNI bloqueante (a main thread passa a rodar o jogo).
        Platform.runLater(() -> doLaunch(row, request));
    }

    /** Spawns KotonohaGameHost and polls its PID until the game exits. */
    private void doLaunch(GameRow row, LaunchRequest request) {
        try {
            String result = request.launchDetached();
            boolean ok = result != null && result.contains("\"ok\":true");
            long pid = -1;
            if (ok && result != null) {
                Matcher matcher = Pattern.compile("\"pid\":(\\d+)").matcher(result);
                if (matcher.find()) pid = Long.parseLong(matcher.group(1));
            }
            logFor(row, "Launch " + (ok ? "OK  " : "ERRO") + " " + row.title.get()
                    + " pid=" + pid + " => " + result);
            showText(result);
            gamesTable.getSelectionModel().select(row);
            if (!ok || pid <= 0) {
                status("Launch ERRO: " + row.title.get());
                logFor(row, "REQUEST " + request.describe().replace('\n', ' '));
                row.running.set(false);
                    gamesTable.refresh();
                return;
            }
            String configuredLog = System.getenv("KOTONOHA_DETACHED_LOG");
            Path log = configuredLog != null && !configuredLog.isBlank()
                    ? Paths.get(configuredLog)
                    : Paths.get(System.getProperty("java.io.tmpdir"),
                                "kotonoha-game-" + pid + ".log");
            hosts.put(pid, new HostState(row, log));
            startHostPolling();
            status("Launched OK: " + row.title.get() + " (PID " + pid + ")");
        } catch (Throwable t) {
            fail("Launch game", t);
            logFor(row, "Launch game ERRO: " + t);
            row.running.set(false);
        } finally {
        }
    }

    private void startHostPolling() {
        if (hostPoll != null) return;
        hostPoll = new Timeline(new KeyFrame(Duration.seconds(1), event -> pollHosts()));
        hostPoll.setCycleCount(Timeline.INDEFINITE);
        hostPoll.play();
    }

    private void pollHosts() {
        for (Map.Entry<Long, HostState> entry : hosts.entrySet()) {
            HostState state = entry.getValue();
            boolean alive = ProcessHandle.of(entry.getKey())
                    .map(ProcessHandle::isAlive).orElse(false);
            if (!alive) {
                state.row.running.set(false);
                status("Game exited: " + state.row.title.get());
                logFor(state.row, "Game host " + entry.getKey() + " exited");
                hosts.remove(entry.getKey());
                if (hosts.isEmpty() && hostPoll != null) {
                    hostPoll.stop();
                    hostPoll = null;
                }
            }
            tailHostLog(state);
        }
        gamesTable.refresh();
    }

    private void tailHostLog(HostState state) {
        try {
            if (!Files.exists(state.logPath)) return;
            byte[] all = Files.readAllBytes(state.logPath);
            if (all.length <= state.offset) return;
            String text = new String(all, (int) state.offset,
                    all.length - (int) state.offset, StandardCharsets.UTF_8);
            state.offset = all.length;
            logFor(state.row, text.stripTrailing());
        } catch (IOException ignored) { }
    }

    private void clearRunning() {
        for (GameRow row : gamesTable.getItems()) row.running.set(false);
    }

    private void status(String message) {
        statusLabel.setText(message);
    }

    private void log(String message) {
        appendLog(message, null);
    }

    /** Escreve no log geral e no log especifico do jogo (chave = caminho do scan). */
    private void logFor(GameRow row, String message) {
        appendLog(message, row);
    }

    private void appendLog(String message, GameRow row) {
        String line = "[" + LocalTime.now().withNano(0) + "] " + message + "\n";
        logArea.appendText(line);
        if (row != null) {
            gameLogs.computeIfAbsent(row.path.get(), key -> new StringBuilder()).append(line);
        }
    }

    /** Chamado pelo ChangeListener de selecao da tabela: mostra so o log do jogo. */
    private void showGameLog(GameRow row) {
        if (row == null) {
            gameLogArea.setText("Selecione um jogo na lista para ver o log.");
            return;
        }
        StringBuilder log = gameLogs.get(row.path.get());
        gameLogArea.setText(log == null || log.length() == 0
                ? "Sem log de launch para " + row.title.get() + ".\n"
                  + "Selecione o jogo e clique em Launch game para gerar o log."
                : log.toString());
        gameLogArea.positionCaret(gameLogArea.getLength());
    }

    private static String nullToEmpty(String value) {
        return value == null ? "" : value;
    }

    private static final class GameRow {
        final SimpleStringProperty title = new SimpleStringProperty();
        final SimpleStringProperty engine = new SimpleStringProperty();
        final SimpleStringProperty saveProfileId = new SimpleStringProperty();
        final SimpleStringProperty path = new SimpleStringProperty();
        // true enquanto o launch deste jogo bloqueia a main thread: linha verde.
        final SimpleBooleanProperty running = new SimpleBooleanProperty(false);
        GameInfo info;
    }

    public static void main(String[] args) {
        attachedMode = Arrays.asList(args).contains("--attached");
        launch(args);
    }
}
