package me.hirameki.kotonoha.bridgetest;

import java.io.File;
import java.time.LocalTime;
import java.util.List;
import java.util.function.Supplier;
import javafx.application.Application;
import javafx.beans.property.SimpleStringProperty;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.geometry.Insets;
import javafx.geometry.Pos;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Label;
import javafx.scene.control.ListView;
import javafx.scene.control.SelectionMode;
import javafx.scene.control.TableColumn;
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

    private final ObservableList<String> roots = FXCollections.observableArrayList();
    private final TableView<GameRow> gamesTable = new TableView<>();
    private final TextArea outputArea = new TextArea();
    private final TextArea logArea = new TextArea();
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
        stage.setScene(new Scene(buildRoot(), 1180, 860));
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
                log("--auto-launch: selecionado " + found.title.get()
                        + " (" + found.path.get() + ")");
                launchSelected(validate);
            }
        }
    }

    private BorderPane buildRoot() {
        BorderPane root = new BorderPane();
        root.setTop(buildTop());
        root.setCenter(buildCenter());
        root.setBottom(buildLog());
        return root;
    }

    private VBox buildTop() {
        Label title = new Label("KotonohaLauncherCore — ponte JNI / launch individual");
        title.setStyle("-fx-font-size: 16px; -fx-font-weight: bold;");

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

        VBox top = new VBox(6, title, addRow, rootsList, options, buildActions());
        top.setPadding(new Insets(10));
        return top;
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

        HBox third = new HBox(8, launchOne, validateRequest, statusLabel);
        third.setAlignment(Pos.CENTER_LEFT);
        HBox.setHgrow(statusLabel, Priority.ALWAYS);

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

    private VBox buildCenter() {
        TableColumn<GameRow, String> colTitle = new TableColumn<>("Título");
        colTitle.setCellValueFactory(c -> c.getValue().title);
        TableColumn<GameRow, String> colEngine = new TableColumn<>("Engine");
        colEngine.setCellValueFactory(c -> c.getValue().engine);
        TableColumn<GameRow, String> colProfile = new TableColumn<>("Save profile");
        colProfile.setCellValueFactory(c -> c.getValue().saveProfileId);
        TableColumn<GameRow, String> colPath = new TableColumn<>("Caminho");
        colPath.setCellValueFactory(c -> c.getValue().path);
        gamesTable.getColumns().addAll(colTitle, colEngine, colProfile, colPath);
        gamesTable.setPrefHeight(184);
        gamesTable.setPlaceholder(new Label("Faça Scan para listar os jogos detectados"));
        gamesTable.getSelectionModel().setSelectionMode(SelectionMode.SINGLE);
        gamesTable.getSelectionModel().selectedItemProperty().addListener(
                (obs, oldRow, row) -> {
                    if (row != null) fillOptionalFields(row.info);
                });
        colTitle.setPrefWidth(190);
        colEngine.setPrefWidth(150);
        colProfile.setPrefWidth(130);
        colPath.setPrefWidth(540);

        outputArea.setEditable(false);
        outputArea.setPromptText("JSON formatado");
        outputArea.setStyle("-fx-font-family: 'Menlo'; -fx-font-size: 11px;");
        VBox.setVgrow(outputArea, Priority.ALWAYS);

        TitledPane tablePane = new TitledPane(
                "Jogos detectados (selecione UM e clique em Launch game)", gamesTable);
        TitledPane paramsPane = new TitledPane(
                "Parâmetros opcionais do launch (da request / do JSON de scan)",
                buildOptionalFields());
        paramsPane.setExpanded(false);
        TitledPane outputPane = new TitledPane("Saída JSON", outputArea);
        VBox.setVgrow(outputPane, Priority.ALWAYS);
        return new VBox(6, tablePane, paramsPane, outputPane);
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
        logArea.setPrefHeight(132);
        logArea.setStyle("-fx-font-family: 'Menlo'; -fx-font-size: 11px;");
        return new VBox(new TitledPane("Log", logArea));
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
            log("Validate request => launch request validated; "
                    + "graphical launch requires SDL app");
            log("REQUEST " + request.describe().replace('\n', ' '));
            showText(request.describe());
            status("Request validada: " + row.title.get());
            return;
        }
        try {
            log("Launch game: " + row.path.get() + " engine="
                    + request.engineSlug + " profile=" + request.saveProfileId
                    + " dryRun=" + request.dryRun);
            status(request.dryRun ? "Validando request..."
                    : "Aguardando jogo... feche a janela para voltar");
            // Chamada sincrona: a main thread passa a rodar o jogo e so'
            // retorna quando a janela e' fechada (ou no dry-run).
            String result = request.launch();
            boolean ok = result != null && result.contains("\"ok\":true");
            boolean playable = JsonUtil.boolField(result, "playable");
            log("Launch " + (ok ? "OK  " : "ERRO") + " " + row.title.get()
                    + " playable=" + playable + " => " + result);
            showText(result);
            status((ok ? "Launched OK: " : "Launch ERRO: ") + row.title.get()
                    + " (playable=" + playable + ")");
            if (result != null && result.contains("\"dryRun\":true")) {
                log("launch request validated; graphical launch requires SDL app");
            } else if (!ok) {
                log("launch request validated; graphical launch requires SDL app"
                        + " (launch real falhou; veja o JSON acima)");
                log("REQUEST " + request.describe().replace('\n', ' '));
            }
        } catch (Throwable t) {
            fail("Launch game", t);
        }
    }

    private void status(String message) {
        statusLabel.setText(message);
    }

    private void log(String message) {
        logArea.appendText("[" + LocalTime.now().withNano(0) + "] " + message + "\n");
    }

    private static String nullToEmpty(String value) {
        return value == null ? "" : value;
    }

    private static final class GameRow {
        final SimpleStringProperty title = new SimpleStringProperty();
        final SimpleStringProperty engine = new SimpleStringProperty();
        final SimpleStringProperty saveProfileId = new SimpleStringProperty();
        final SimpleStringProperty path = new SimpleStringProperty();
        GameInfo info;
    }

    public static void main(String[] args) {
        launch(args);
    }
}
