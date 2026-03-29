#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <string.h> 
#include <HTTPClient.h>

//@TradutorMorseBot

// --- Configurações WiFi ---
// const char* ssid = "nome_da_rede";
// const char* password = "senha_da_rede";


// --- Configurações Telegram ---
#define BOT_TOKEN "8415451527:AAH_PdB19DBFTxVX7Bb-AalmIp-jz1CxR3s" 

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// --- Controle de tempo ---
unsigned long lastTimeCheck = 0;
const unsigned long checkInterval = 2000;

// --- Hardware ---
#define BOTAO 27
#define BUZZER 25
#define LED 26
#define BOTAO_ENVIAR 33

// --- Configurações Morse ---
#define TEMPO_SEGURO 200           // define se é . ou -
#define UNIDADE_TEMPO 200          // tempo base
#define TEMPO_ENTRE_LETRAS 1000    // pausa entre letras
#define TEMPO_ENTRE_PALAVRAS 2000  // pausa entre palavras

// --- Estados ---
unsigned long tempoPressionado = 0;
unsigned long tempoSolto = 0;
bool estadoAnteriorLeitura = HIGH;
bool botaoPressionado = false;
bool pausaProcessada = false;
const int debounceDelay = 50;
unsigned long ultimoDebounce = 0;
bool estadoEstavel = HIGH;
int modo_operacao = 2;  

String codigo_morse = "";
String ultimo_chat_id = "";

// --- Tabelas Morse ---
const char *letras = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const char *morse[] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
  "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
  "..-", "...-", ".--", "-..-", "-.--", "--..",
  "-----", ".----", "..---", "...--", "....-", ".....",
  "-....", "--...", "---..", "----."
};

// --- Funções ---
void connectWiFi();
void sendMenu(String chat_id, String nome);
void pegarMensagemTelegram(int numNewMessages);
void pegarMensagemMorse();
void texto_para_morse(const char *texto);
void enviarMensagemMorse();
String morse_para_texto(String codigo);

// ----------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  connectWiFi();

  pinMode(BOTAO, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(BOTAO_ENVIAR, INPUT_PULLUP);
}

// ----------------------------------------------------------------------

void loop() {
  pegarMensagemMorse();

  if (modo_operacao == 2 || (digitalRead(BOTAO_ENVIAR) == LOW && digitalRead(BOTAO) == LOW)) {
    if (millis() - lastTimeCheck > checkInterval) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      if (numNewMessages) {
        pegarMensagemTelegram(numNewMessages);
      }
      lastTimeCheck = millis();
    }
  }
}

// ----------------------------------------------------------------------

void connectWiFi() {
  Serial.printf("Conectando ao WiFi: %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  client.setInsecure();

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.print("✅ Conectado! IP: ");
  Serial.println(WiFi.localIP());
}

// ----------------------------------------------------------------------

void sendMenu(String chat_id, String nome) {
  HTTPClient https;
  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendMessage";
  https.begin(url);
  https.addHeader("Content-Type", "application/json");

  String keyboardJson =
    "{\"keyboard\":[[{\"text\":\"Morse->Texto\"},{\"text\":\"Texto->Morse\"}]],"
    "\"resize_keyboard\":true}";

  String mensagem =
    "👋 *Bem-vindo(a), " + nome + "!*\n\n"
    "Escolha que modo de tradução você deseja:\n\n"
    "Modo 1: Morse → Texto\n"
    "Modo 2: Texto → Morse";

  String payload =
    "{"
      "\"chat_id\":\"" + chat_id + "\","
      "\"text\":\"" + mensagem + "\","
      "\"parse_mode\":\"Markdown\","
      "\"reply_markup\":" + keyboardJson +
    "}";

  int httpCode = https.POST(payload);
  String response = https.getString();

  https.end();
}


// ----------------------------------------------------------------------

void pegarMensagemTelegram(int numNewMessages) {
  Serial.printf("📩 Novas mensagens: %d\n", numNewMessages);

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String msgText = bot.messages[i].text;
    String fromName = bot.messages[i].from_name;
    if (fromName == "") fromName = "usuário";

    ultimo_chat_id = chat_id;

    Serial.printf("Mensagem de %s: %s\n", fromName.c_str(), msgText.c_str());

    if (msgText == "/start" || msgText == "Iniciar") {
      sendMenu(chat_id, fromName);
    } 
    else if (msgText.startsWith("Morse->Texto")) {
      modo_operacao = 1;
      bot.sendMessage(chat_id, "Modo ativado: *Morse → Texto*\nO botão físico agora traduz para texto.", "Markdown");
    }
    else if (msgText.startsWith("Texto->Morse")) {
      modo_operacao = 2;
      bot.sendMessage(chat_id, "Modo ativado: *Texto → Morse*\nEnvie uma mensagem que eu toco o código morse.", "Markdown");
    }
    else {
      bot.sendMessage(chat_id, "🔊 Tocando: " + msgText);
      texto_para_morse(msgText.c_str());
      modo_operacao = 2;
    }
  }
}

// ----------------------------------------------------------------------

void pegarMensagemMorse() {
  if (modo_operacao != 1) return;
  bool leituraAtual = digitalRead(BOTAO);
  if (leituraAtual != estadoAnteriorLeitura) {
    ultimoDebounce = millis();
  }

  // só aceita a nova leitura se passou o tempo de debounce
  if ((millis() - ultimoDebounce) > debounceDelay) {
    if (leituraAtual != estadoEstavel) {
      estadoEstavel = leituraAtual;
      if (estadoEstavel == LOW) {
        // botão acabou de ser pressionado
        tempoPressionado = millis();
        botaoPressionado = true;
        pausaProcessada = false;
        tone(BUZZER, 5000);
        digitalWrite(LED, HIGH);
      } else {
        // botão acabou de ser solto
        if (botaoPressionado) {
          unsigned long duracao = millis() - tempoPressionado;
          noTone(BUZZER);
          digitalWrite(LED, LOW);
          if (duracao <= TEMPO_SEGURO) {
            codigo_morse += ".";
            Serial.print(".");
          } else {
            codigo_morse += "-";
            Serial.print("-");
          }
          tempoSolto = millis();
          botaoPressionado = false;
        } else {
            // Se não estava marcado como pressionado, só garante off
            noTone(BUZZER);
            digitalWrite(LED, LOW);
          }
      }
    }
  }
  // verifica pausas
  if (!botaoPressionado) {
    unsigned long tempoAtual = millis() - tempoSolto;
    //insere espaço entre letras
    if (tempoAtual > TEMPO_ENTRE_LETRAS && (codigo_morse.endsWith(".") || codigo_morse.endsWith("-"))) {
      codigo_morse += " ";
      Serial.print(" ");
    }
      //insere espaço entre palavras
    if (tempoAtual >= TEMPO_ENTRE_PALAVRAS && codigo_morse.endsWith(" ")){      
      codigo_morse += "  ";
      Serial.print("  ");
      tempoSolto = millis();
    }
    //  envia tradução
    else if (codigo_morse.length() > 0 && digitalRead(BOTAO_ENVIAR) == LOW) {
      enviarMensagemMorse();
      Serial.print("\n");
    }
  }
  // atualiza leitura anterior
  estadoAnteriorLeitura = leituraAtual;
}

// ----------------------------------------------------------------------

void enviarMensagemMorse() {

  String traduzido = morse_para_texto(codigo_morse);

  if (ultimo_chat_id != "") {
    bot.sendMessage(ultimo_chat_id, "🔤 Código: " + codigo_morse);
    bot.sendMessage(ultimo_chat_id, "🔤 Tradução: " + traduzido);
  }

  // limpa buffer
  codigo_morse = "";
  pausaProcessada = true;
}

// ----------------------------------------------------------------------

void texto_para_morse(const char *texto) {
  for (int i = 0; texto[i] != '\0'; i++) {
    char c = toupper(texto[i]);

    if (c == ' ') {
      delay(UNIDADE_TEMPO * 7);
      continue;
    }
    const char *ptr = strchr(letras, c);
    if (ptr) {
      int idx = ptr - letras;
      const char *codigo = morse[idx];

      for (int j = 0; codigo[j] != '\0'; j++) {
        tone(BUZZER, 5000);
        digitalWrite(LED, HIGH);

        if (codigo[j] == '.')
          delay(UNIDADE_TEMPO);
        else
          delay(UNIDADE_TEMPO * 4);

        noTone(BUZZER);
        digitalWrite(LED, LOW);
        delay(UNIDADE_TEMPO);
      }
      delay(UNIDADE_TEMPO * 3);  // pausa entre letras
    }
  }
}

// ----------------------------------------------------------------------

String morse_para_texto(String codigo) {
  String saida = "";
  String buffer = "";

  for (int i = 0; i <= codigo.length(); i++) {
    char c = codigo[i];

    if (c != ' ' && c != '\0') {
      buffer += c;
    } else {
      if (buffer.length() > 0) {
        for (int j = 0; j < strlen(letras); j++) {
          if (buffer == morse[j]) {
            saida += letras[j];
            break;
          }
        }
        buffer = "";
      }

      // duas pausas = espaço entre palavras
      if (codigo[i] == ' ' && codigo[i + 1] == ' ') {
        saida += " ";
        i++;
      }
    }
  }

  return saida;
}


