package me.hirameki.kotonoha;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

public class MainActivity extends Activity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ExtractAssets.copyAssetsToDataFiles(this);

        final EditText editText = findViewById(R.id.custom_edit_text);

        findViewById(R.id.button_generic).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                String[] args = getArgsFromInput(editText);
                startKotonohaWithArguments(Kotonoha.TARGET_GENERIC, args);
            }
        });

        findViewById(R.id.button_school_days).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                String[] args = getArgsFromInput(editText);
                startKotonohaWithArguments(Kotonoha.TARGET_SCHOOL_DAYS, args);
            }
        });

        findViewById(R.id.button_zero).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                String[] args = getArgsFromInput(editText);
                startKotonohaWithArguments(Kotonoha.TARGET_ZERO_NO_TSUKAIMA, args);
            }
        });

        findViewById(R.id.button_snow).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                String[] args = getArgsFromInput(editText);
                startKotonohaWithArguments(Kotonoha.TARGET_SNOW_RADISH, args);
            }
        });

        findViewById(R.id.button_ddlc).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                String[] args = getArgsFromInput(editText);
                startKotonohaWithArguments(Kotonoha.TARGET_DDLC, args);
            }
        });
    }

    private String[] getArgsFromInput(EditText editText) {
        if (editText == null) return new String[0];
        String text = editText.getText().toString().trim();
        if (text.isEmpty()) return new String[0];
        return text.split("\\s+");
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        final String resultMessage;
        if (resultCode == RESULT_OK) {
            resultMessage = "Execução concluída com sucesso (RESULT_OK)";
        } else if (resultCode == RESULT_CANCELED) {
            resultMessage = "Execução cancelada (RESULT_CANCELED)";
        } else {
            resultMessage = "Retorno da Intent (Code: " + resultCode + ")";
        }

        String details = resultMessage;
        if (data != null) {
            Bundle extras = data.getExtras();
            if (extras != null) {
                details = resultMessage + " | Extras: " + extras;
            }
            String action = data.getAction();
            if (action != null) {
                details += " | Action: " + action;
            }
        }

        Toast.makeText(this, details, Toast.LENGTH_LONG).show();
        TextView resultTextView = findViewById(R.id.result_text_view);
        if (resultTextView != null) {
            resultTextView.setText(details);
        }
    }

    private void startKotonohaWithArguments(String target, String[] arguments) {
        Kotonoha.setArguments(arguments);
        Intent intent = new Intent(this, Kotonoha.class);
        intent.putExtra(Kotonoha.EXTRA_TARGET, target);
        intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivityForResult(intent, 7007);
    }
}
