package me.hirameki.kotonoha;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.EditText;

public class MainActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ExtractAssets.copyAssetsToDataFiles(this);


        findViewById(R.id.button_school_days).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                startKotonohaWithArguments(new String[]{
                        "-s", "assets/styles.skot",
                        "-p", "http://kotonoha.hirameki.me/SchoolDays/",
                        "-l", "assets/00/00-00-A00.ENG.ORS",
                        "-l", "assets/00/00-00-A01.ENG.ORS",
                        "-l", "assets/00/00-00-A02.ENG.ORS",
                        "-l", "assets/00/00-00-A03.ENG.ORS",
                });
            }
        });

        findViewById(R.id.button_cross_days).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                startKotonohaWithArguments(new String[]{
                        "-s", "assets/stylesJP.skot",
                        "-p", "http://kotonoha.hirameki.me/CrossDays/",
                        "-l", "assets/01/01-00-A00.ORS"
                });
            }
        });

        findViewById(R.id.button_custom_params).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                EditText editText = findViewById(R.id.custom_edit_text);
                String customParams = editText.getText().toString();
                String[] args = customParams.split(" ");
                startKotonohaWithArguments(args);
            }
        });

    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        finishAffinity();
        System.exit(0);
    }

    private void startKotonohaWithArguments(String[] arguments) {
        Kotonoha.setArguments(arguments);
        Intent intent = new Intent(this, Kotonoha.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivityForResult(intent, 1);
    }

}
