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
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.stage.Stage;
import me.hirameki.kotonoha.launcher.KotonohaLauncherBridge;

/** Minimal JavaFX UI for exercising the KotonohaLauncherCore JNI bridge. */
public final class BridgeTestApp extends Application {
    private static final String SERVER_ROOT = "/Volumes/Server/data2/Games/DDLC";
    private static final String EXTERNAL_ROOT = "/Volumes/Nero 2TB/KotonohaAssets/DDLC";

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

        stage.setTitle("Kotonoha JNI Bridge Test (JavaFX)");
        stage.setScene(new Scene(buildRoot(), 1120, 780));
        stage.show();
        log("Pronto. Use Add folder / Scan / Detect / Params / Upscale / Validate.");

        // Modo de verificação automática (sem automação de mouse/Accessibility).
        if (getParameters().getRaw().contains("--auto-scan")) {
            log("--auto-scan: disparando Scan e Detect pela mesma lógica dos botões");
            run("Scan", () -> KotonohaLauncherBridge.nativeScanRootsJson(
                    roots.toArray(new String[0]), splitBox.isSelected(),
                    nullToEmpty(saveRootField.getText())));
            run("Detect", () -> KotonohaLauncherBridge.nativeDetectPathJson(
                    nullToEmpty(detectField.getText()), splitBox.isSelected(),
                    nullToEmpty(saveRootField.getText())));
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
        Label title = new Label("KotonohaLauncherCore — ponte JNI");
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
        rootsList.setPrefHeight(74);

        HBox options = new HBox(10, splitBox, new Label("Custom save root:"), saveRootField);
        options.setAlignment(Pos.CENTER_LEFT);
        HBox.setHgrow(saveRootField, Priority.ALWAYS);

        VBox top = new VBox(8, title, addRow, rootsList, options, buildActions());
        top.setPadding(new Insets(12));
        return top;
    }

    private ListView<String> rootsList;

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
                String json = "{\"saved\":" + ok + ",\"profile\":\"" + profile
                        + "\",\"ktrfPath\":\"" + ktrfField.getText()
                        + "\",\"gamePath\":\"" + firstRoot + "\"}";
                log("nativeSaveGameParams -> " + ok);
                show(json);
            } catch (Throwable t) {
                fail("Params: save", t);
            }
        });

        Button presets = button("Upscale presets",
                KotonohaLauncherBridge::nativeUpscalePresetsJson);
        presets.setTooltip(new Tooltip("nativeUpscalePresetsJson()"));

        Button defaultRoute = button("Default route",
                KotonohaLauncherBridge::nativeDefaultSchoolDaysRoute);
        defaultRoute.setTooltip(new Tooltip("nativeDefaultSchoolDaysRoute()"));

        Button validate = button("Validate route", () -> KotonohaLauncherBridge
                .nativeValidateRoute(nullToEmpty(ktrfField.getText())));
        validate.setTooltip(new Tooltip("nativeValidateRoute(ktrfPath)"));

        Button resolveSave = button("Resolve save dir", () -> KotonohaLauncherBridge
                .nativeResolveSaveDirectory(
                        nullToEmpty(detectField.getText()), "school-days",
                        nullToEmpty(profileField.getText()), splitBox.isSelected(),
                        nullToEmpty(saveRootField.getText())));
        resolveSave.setTooltip(new Tooltip("nativeResolveSaveDirectory(...)"));

        Button launchAll = new Button("Launch all detected");
        launchAll.setTooltip(new Tooltip("nativeLaunchJson para cada jogo da tabela"));
        launchAll.setOnAction(e -> {
            int ok = 0, failed = 0;
            for (GameRow game : gamesTable.getItems()) {
                String path = game.path.get();
                if (path.isEmpty()) continue;
                String result = KotonohaLauncherBridge.nativeLaunchJson(
                        path, game.engine.get(), nullToEmpty(profileField.getText()),
                        splitBox.isSelected(), nullToEmpty(saveRootField.getText()),
                        "", "", "", "", "", "", "", "", "", "", "");
                if (result != null && result.contains("\"ok\":true")) ++ok;
                else ++failed;
                log("Launch " + path + " => " + result);
            }
            log("Launch all: ok=" + ok + " fail=" + failed);
        });

        HBox first = new HBox(8, scan, detect, presets, defaultRoute, resolveSave);
        HBox second = new HBox(8, loadParams, saveParams, validate,
                new Label("Profile:"), profileField,
                new Label("Path / .ktrf:"), detectField, ktrfField);
        second.setAlignment(Pos.CENTER_LEFT);
        HBox.setHgrow(ktrfField, Priority.ALWAYS);
        ktrfField.setPrefWidth(240);

        TitledPane pane = new TitledPane("Ações da ponte JNI (chamadas nativas reais)",
                new VBox(6, first, new HBox(8, new Label("Detect path:"), detectField),
                        second, launchAll));
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
        TableColumn<GameRow, String> colPath = new TableColumn<>("Caminho");
        colPath.setCellValueFactory(c -> c.getValue().path);
        gamesTable.getColumns().addAll(colTitle, colEngine, colPath);
        gamesTable.setPrefHeight(210);
        gamesTable.setPlaceholder(new Label("Faça Scan para listar os jogos detectados"));
        colTitle.setPrefWidth(210);
        colEngine.setPrefWidth(150);
        colPath.setPrefWidth(520);

        outputArea.setEditable(false);
        outputArea.setPromptText("JSON formatado");
        outputArea.setStyle("-fx-font-family: 'Menlo'; -fx-font-size: 11px;");
        VBox.setVgrow(outputArea, Priority.ALWAYS);

        TitledPane tablePane = new TitledPane("Jogos detectados (parse simples do JSON)", gamesTable);
        TitledPane outputPane = new TitledPane("Saída JSON", outputArea);
        VBox.setVgrow(outputPane, Priority.ALWAYS);
        return new VBox(8, tablePane, outputPane);
    }

    private VBox buildLog() {
        logArea.setEditable(false);
        logArea.setPrefHeight(150);
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
            show(result);
        } catch (Throwable t) {
            fail(name, t);
        }
    }

    private void fail(String name, Throwable t) {
        log(name + " ERRO: " + t);
        outputArea.setText(String.valueOf(t));
    }

    private void show(String json) {
        outputArea.setText(JsonUtil.pretty(json));
        List<String[]> parsed = JsonUtil.games(json);
        gamesTable.getItems().setAll(parsed.stream().map(row -> {
            GameRow g = new GameRow();
            g.title.set(row[0]);
            g.engine.set(row[1]);
            g.path.set(row[2]);
            return g;
        }).toList());
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
        final SimpleStringProperty path = new SimpleStringProperty();
    }

    public static void main(String[] args) {
        launch(args);
    }
}
