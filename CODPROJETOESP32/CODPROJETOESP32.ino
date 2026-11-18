
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <math.h>
#include <driver/i2s.h>

#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(16384);
#endif

// O AP "TecBot-Config" fica disponivel para configuracao em
// http://192.168.4.1, inclusive enquanto o ESP32 tenta conectar ao Wi-Fi.
// Depois de conectado, tudo pode ser alterado pelo dashboard no navegador.
const char* AP_PROVISIONAMENTO_SSID = "TecBot-Config";
const char* AP_PROVISIONAMENTO_SENHA = "TroqueSuaSenha123";
String ESP32_DEVICE_TOKEN = "";

const int OLED_LARGURA = 128;
const int OLED_ALTURA = 64;
const int OLED_RESET = -1;
const int PINO_OLED_SDA = 21;
const int PINO_OLED_SCL = 22;
const int PINO_HCSR04_TRIG = 5;
const int PINO_HCSR04_ECHO = 18;
const int PINO_MICROFONE_BCLK = 14;
const int PINO_MICROFONE_WS = 15;
const int PINO_MICROFONE_DOUT = 32;
const int PINO_AMPLIFICADOR_LRC = 19;
const int PINO_AMPLIFICADOR_BCLK = 27;
const int PINO_AMPLIFICADOR_DIN = 23;
const i2s_port_t PORTA_I2S_MICROFONE = I2S_NUM_0;
const i2s_port_t PORTA_I2S_AMPLIFICADOR = I2S_NUM_1;
const uint32_t TAXA_AMOSTRAGEM_AUDIO = 16000;
const size_t AMOSTRAS_BLOCO_AUDIO = 256;
const size_t DURACAO_MAXIMA_AUDIO_MS = 5000;
const size_t DURACAO_MINIMA_VOZ_AUDIO_MS = 180;
const uint8_t BLOCOS_VOZ_CONSECUTIVOS_MINIMOS = 3;
const size_t MAXIMO_AMOSTRAS_AUDIO =
  TAXA_AMOSTRAGEM_AUDIO * DURACAO_MAXIMA_AUDIO_MS / 1000;
const size_t ALINHAMENTO_BLOCO_ADPCM = 256;
const size_t AMOSTRAS_BLOCO_ADPCM =
  (ALINHAMENTO_BLOCO_ADPCM - 4) * 2 + 1;
const size_t MAXIMO_BLOCOS_AUDIO =
  (MAXIMO_AMOSTRAS_AUDIO + AMOSTRAS_BLOCO_ADPCM - 1) /
  AMOSTRAS_BLOCO_ADPCM;
const size_t TAMANHO_CABECALHO_WAV_ADPCM = 60;
const uint32_t LIMIAR_AUDIO_RMS = 350;
const unsigned long DURACAO_SILENCIO_AUDIO_MS = 800;
const unsigned long INTERVALO_CONSULTA_SERVIDOR = 3000;
const unsigned long INTERVALO_HEARTBEAT_SERVIDOR = 20000;
const unsigned long INTERVALO_LEITURA_HCSR04 = 3000;
const unsigned long TIMEOUT_SERVIDOR = 1500;
const unsigned long TIMEOUT_POST_SENSOR = 800;
const unsigned long TIMEOUT_ECHO_HCSR04_US = 25000;
const unsigned long INTERVALO_PAGINA_OLED = 2500;
const unsigned long DURACAO_ABERTURA_MS = 3000;
const unsigned long INTERVALO_ANIMACAO_ROSTO_MS = 250;
const unsigned long DURACAO_CARINHA_FALANDO_MS = 8000;
const size_t TAMANHO_MAXIMO_URL_SERVIDOR = 200;
const size_t LARGURA_LINHA_OLED = 21;
const int LINHAS_MENSAGEM_OLED = 4;
const char* ID_DISPOSITIVO = "esp32-1";
const size_t TAMANHO_MAXIMO_TRANSCRICAO = 4000;

enum EstadoAudio {
  AUDIO_AGUARDANDO,
  AUDIO_GRAVANDO,
  AUDIO_ENVIANDO,
  AUDIO_PROCESSANDO,
  AUDIO_MOSTRANDO_TRANSCRICAO,
  AUDIO_REPRODUZINDO,
  AUDIO_MIC_DESATIVADO,
  AUDIO_SOM_DESATIVADO,
  AUDIO_CONCLUIDO,
  AUDIO_ERRO_CONEXAO,
  AUDIO_ERRO_MICROFONE,
  AUDIO_ERRO_AUDIO
};

void definirEstadoAudio(EstadoAudio novoEstado);
void finalizarTransacaoAudio(EstadoAudio estadoFinal);

WebServer servidor(80);
Preferences preferencias;
Adafruit_SSD1306 tela(OLED_LARGURA, OLED_ALTURA, &Wire, OLED_RESET);
uint8_t bufferAudioWav[
  TAMANHO_CABECALHO_WAV_ADPCM +
  MAXIMO_BLOCOS_AUDIO * ALINHAMENTO_BLOCO_ADPCM];
int32_t bufferAmostrasMicrofone[AMOSTRAS_BLOCO_AUDIO];

String ssidConfigurado = "";
String ssidTentativa = "";
String senhaTentativa = "";
String urlServidor = "";
String estadoServidorOLED = "sem URL";
String mensagemOLED = "Configure o Wi-Fi e o servidor.";
size_t deslocamentoMensagemOLED = 0;
bool telaDisponivel = false;
bool existeProximaPaginaOLED = false;
bool falaEmAndamento = false;
bool modoHorarioAtivo = false;
bool telaOLEDAtiva = true;
uint8_t contrasteOLED = 180;
String textoInicialOLED = "TecBot pronto!";
bool wifiConectadoMostrado = false;
bool animacaoBocaAberta = false;
bool aberturaAtiva = true;
bool microfoneDisponivel = false;
bool microfoneInicializacaoPendente = false;
bool amplificadorDisponivel = false;
bool microfoneAtivo = true;
bool somAtivo = true;
uint8_t volumeSom = 70;
bool capturandoAudio = false;
bool audioTransacaoPendente = false;
volatile bool audioTransacaoAtiva = false;
bool pausaMicrofonePosAudioAtiva = false;
EstadoAudio estadoAudio = AUDIO_AGUARDANDO;
EstadoAudio estadoAudioExibido = AUDIO_AGUARDANDO;
char transcricaoAudio[TAMANHO_MAXIMO_TRANSCRICAO + 1] = {};
unsigned long fimMensagemAudio = 0;
size_t tamanhoAudioCapturado = 0;
size_t quantidadeAmostrasAudio = 0;
size_t amostrasVozAudio = 0;
uint8_t blocosVozConsecutivosAudio = 0;
size_t blocosAudio = 0;
size_t amostrasBlocoAudio = 0;
int16_t preditorAdpcm = 0;
int indiceAdpcm = 0;
unsigned long inicioSilencioAudio = 0;
unsigned long capturaMicrofoneBloqueadaAte = 0;
time_t horaServidorEpoch = 0;
unsigned long millisHoraServidor = 0;

unsigned long inicioConexao = 0;
unsigned long ultimaConsultaServidor = 0;
unsigned long ultimoHeartbeatServidor = 0;
unsigned long ultimaLeituraHCSR04 = 0;
unsigned long ultimaPaginaOLED = 0;
unsigned long inicioAbertura = 0;
unsigned long ultimaAnimacaoRosto = 0;
unsigned long inicioResposta = 0;
bool tentandoConectar = false;
bool modoProvisionamentoAtivo = false;
unsigned long ultimaTentativaProvisionamento = 0;
bool reinicioPendente = false;
unsigned long horarioReinicio = 0;
uint64_t ultimoIdExibido = 0;

bool validarUrlServidor(String url) {
  url.trim();
  if (url.length() == 0 || url.length() > TAMANHO_MAXIMO_URL_SERVIDOR ||
      !url.startsWith("http://") || url.indexOf('?') >= 0 ||
      url.indexOf('#') >= 0 || url.indexOf('\\') >= 0) {
    return false;
  }

  for (size_t i = 0; i < url.length(); i++) {
    if ((unsigned char)url[i] <= 32 || (unsigned char)url[i] == 127) {
      return false;
    }
  }

  String restante = url.substring(7);
  int fimAutoridade = restante.indexOf('/');
  String autoridade = fimAutoridade >= 0
    ? restante.substring(0, fimAutoridade)
    : restante;
  if (autoridade.length() == 0 || autoridade.indexOf('@') >= 0) {
    return false;
  }

  String host = autoridade;
  int separadorPorta = autoridade.lastIndexOf(':');
  if (separadorPorta >= 0) {
    host = autoridade.substring(0, separadorPorta);
    String porta = autoridade.substring(separadorPorta + 1);
    if (porta.length() == 0) return false;

    unsigned long numeroPorta = 0;
    for (size_t i = 0; i < porta.length(); i++) {
      if (porta[i] < '0' || porta[i] > '9') return false;
      numeroPorta = numeroPorta * 10 + (porta[i] - '0');
      if (numeroPorta > 65535) return false;
    }
    if (numeroPorta == 0) return false;
  }

  if (host.length() == 0 || host.startsWith(".") || host.endsWith(".") ||
      host.indexOf("..") >= 0 || host.indexOf(':') >= 0) {
    return false;
  }

  String hostMinusculo = host;
  hostMinusculo.toLowerCase();
  if (hostMinusculo == "localhost" ||
      hostMinusculo.endsWith(".localhost") ||
      hostMinusculo == "127" ||
      hostMinusculo.startsWith("127.") ||
      hostMinusculo == "0.0.0.0") {
    return false;
  }

  bool inicioRotulo = true;
  for (size_t i = 0; i < host.length(); i++) {
    char caractere = host[i];
    if (caractere == '.') {
      if (inicioRotulo || host[i - 1] == '-') return false;
      inicioRotulo = true;
    } else if ((caractere >= 'a' && caractere <= 'z') ||
               (caractere >= 'A' && caractere <= 'Z') ||
               (caractere >= '0' && caractere <= '9') ||
               caractere == '-') {
      if (inicioRotulo && caractere == '-') return false;
      inicioRotulo = false;
    } else {
      return false;
    }
  }
  if (inicioRotulo || host.endsWith("-")) return false;

  return true;
}

bool textoValido(const String& texto, size_t maximo) {
  if (texto.length() == 0 || texto.length() > maximo) return false;
  for (size_t i = 0; i < texto.length(); i++) {
    unsigned char caractere = (unsigned char)texto[i];
    if (caractere < 32 || caractere == 127) return false;
  }
  return true;
}

bool tokenValido(const String& token) {
  if (token.length() < 1 || token.length() > 256) return false;
  for (size_t i = 0; i < token.length(); i++) {
    unsigned char caractere = (unsigned char)token[i];
    if (caractere < 33 || caractere > 126) return false;
  }
  return true;
}

String normalizarTextoOLED(const String& texto) {
  String resultado;
  resultado.reserve(texto.length());

  for (size_t i = 0; i < texto.length(); i++) {
    unsigned char caractere = (unsigned char)texto[i];
    if (caractere < 128) {
      resultado += (char)caractere;
      continue;
    }

    if (caractere == 0xC3 && i + 1 < texto.length()) {
      unsigned char acento = (unsigned char)texto[i + 1];
      i++;
      if ((acento >= 0x80 && acento <= 0x85) ||
          (acento >= 0xA0 && acento <= 0xA5)) {
        resultado += 'a';
      } else if (acento == 0x87 || acento == 0xA7) {
        resultado += 'c';
      } else if ((acento >= 0x88 && acento <= 0x8B) ||
                 (acento >= 0xA8 && acento <= 0xAB)) {
        resultado += 'e';
      } else if ((acento >= 0x8C && acento <= 0x8F) ||
                 (acento >= 0xAC && acento <= 0xAF)) {
        resultado += 'i';
      } else if (acento == 0x91 || acento == 0xB1) {
        resultado += 'n';
      } else if ((acento >= 0x92 && acento <= 0x96) ||
                 (acento >= 0xB2 && acento <= 0xB6)) {
        resultado += 'o';
      } else if ((acento >= 0x99 && acento <= 0x9C) ||
                 (acento >= 0xB9 && acento <= 0xBC)) {
        resultado += 'u';
      } else if (acento == 0x9D || acento == 0xBD || acento == 0xBF) {
        resultado += 'y';
      } else if (acento == 0x9F) {
        resultado += "ss";
      }
    }
  }
  return resultado;
}

String proximaLinhaOLED(size_t& deslocamento) {
  String linha;
  while (deslocamento < mensagemOLED.length()) {
    char caractere = mensagemOLED[deslocamento];
    if (caractere == '\r') {
      deslocamento++;
      continue;
    }
    if (caractere == '\n') {
      deslocamento++;
      break;
    }
    if (caractere == ' ' || caractere == '\t') {
      if (linha.length() == 0) {
        deslocamento++;
        continue;
      }
      size_t inicioPalavra = deslocamento;
      while (deslocamento < mensagemOLED.length() &&
             (mensagemOLED[deslocamento] == ' ' ||
              mensagemOLED[deslocamento] == '\t')) {
        deslocamento++;
      }
      if (deslocamento >= mensagemOLED.length() ||
          mensagemOLED[deslocamento] == '\n') {
        continue;
      }
      size_t fimPalavra = deslocamento;
      while (fimPalavra < mensagemOLED.length() &&
             mensagemOLED[fimPalavra] != ' ' &&
             mensagemOLED[fimPalavra] != '\t' &&
             mensagemOLED[fimPalavra] != '\n' &&
             mensagemOLED[fimPalavra] != '\r') {
        fimPalavra++;
      }
      size_t tamanhoPalavra = fimPalavra - deslocamento;
      if (linha.length() + 1 + tamanhoPalavra > LARGURA_LINHA_OLED) {
        deslocamento = inicioPalavra;
        break;
      }
      linha += ' ';
      linha += mensagemOLED.substring(deslocamento, fimPalavra);
      deslocamento = fimPalavra;
      continue;
    }

    size_t fimPalavra = deslocamento;
    while (fimPalavra < mensagemOLED.length() &&
           mensagemOLED[fimPalavra] != ' ' &&
           mensagemOLED[fimPalavra] != '\t' &&
           mensagemOLED[fimPalavra] != '\n' &&
           mensagemOLED[fimPalavra] != '\r') {
      fimPalavra++;
    }
    size_t tamanhoPalavra = fimPalavra - deslocamento;
    size_t espaco = linha.length() > 0 ? 1 : 0;
    if (linha.length() + espaco + tamanhoPalavra <= LARGURA_LINHA_OLED) {
      if (espaco) linha += ' ';
      linha += mensagemOLED.substring(deslocamento, fimPalavra);
      deslocamento = fimPalavra;
    } else if (linha.length() == 0) {
      linha = mensagemOLED.substring(
        deslocamento, deslocamento + LARGURA_LINHA_OLED);
      deslocamento += LARGURA_LINHA_OLED;
      break;
    } else {
      break;
    }
  }
  return linha;
}

bool horaSincronizada() {
  return horaServidorEpoch > 0;
}

String formatarHoraAtual() {
  unsigned long totalSegundos;
  if (horaSincronizada()) {
    totalSegundos = (unsigned long)horaServidorEpoch + (millis() - millisHoraServidor) / 1000;
  } else {
    totalSegundos = millis() / 1000;
  }
  long segundosDia = (long)(totalSegundos % 86400UL) - 3 * 3600;
  if (segundosDia < 0) segundosDia += 86400;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02ld:%02ld:%02ld",
           segundosDia / 3600, (segundosDia % 3600) / 60, segundosDia % 60);
  return String(buffer);
}

String formatarDataAtual() {
  if (!horaSincronizada()) return String("--/--/----");
  time_t agora = horaServidorEpoch + (time_t)((millis() - millisHoraServidor) / 1000) - 3 * 3600;
  struct tm* tempo = gmtime(&agora);
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d",
           tempo->tm_mday, tempo->tm_mon + 1, tempo->tm_year + 1900);
  return String(buffer);
}

long diasDesde1970(int ano, int mes, int dia) {
  ano -= mes <= 2;
  long era = (ano >= 0 ? ano : ano - 399) / 400;
  unsigned long anoEra = (unsigned long)(ano - era * 400);
  unsigned long diaAno = (153UL * (mes + (mes > 2 ? -3 : 9)) + 2) / 5 + dia - 1;
  unsigned long diaEra = anoEra * 365 + anoEra / 4 - anoEra / 100 + diaAno;
  return era * 146097L + (long)diaEra - 719468L;
}

time_t parsearIso8601(const String& texto) {
  int ano = 0, mes = 0, dia = 0, hora = 0, minuto = 0, segundo = 0;
  if (sscanf(texto.c_str(), "%d-%d-%dT%d:%d:%d", &ano, &mes, &dia, &hora, &minuto, &segundo) != 6) {
    return 0;
  }
  return (time_t)(diasDesde1970(ano, mes, dia) * 86400L +
                  hora * 3600L + minuto * 60L + segundo);
}

void desenharIconeWifi(int x, int y, bool ativo) {
  tela.drawLine(x, y + 3, x + 4, y, SSD1306_WHITE);
  tela.drawLine(x + 4, y, x + 8, y + 3, SSD1306_WHITE);
  tela.drawLine(x + 2, y + 6, x + 4, y + 4, SSD1306_WHITE);
  tela.drawLine(x + 4, y + 4, x + 6, y + 6, SSD1306_WHITE);
  tela.fillCircle(x + 4, y + 9, 1, SSD1306_WHITE);
  if (!ativo) tela.drawLine(x - 1, y, x + 9, y + 10, SSD1306_WHITE);
}

void desenharIconeServidor(int x, int y, bool ativo) {
  tela.drawRoundRect(x, y, 10, 5, 1, SSD1306_WHITE);
  tela.drawRoundRect(x, y + 6, 10, 5, 1, SSD1306_WHITE);
  tela.fillCircle(x + 2, y + 2, 1, SSD1306_WHITE);
  tela.fillCircle(x + 2, y + 8, 1, SSD1306_WHITE);
  if (!ativo) tela.drawLine(x - 1, y, x + 10, y + 11, SSD1306_WHITE);
}

void desenharIconeMicrofone(int x, int y, bool ativo) {
  tela.drawRoundRect(x + 2, y, 5, 8, 2, SSD1306_WHITE);
  tela.drawLine(x, y + 4, x, y + 6, SSD1306_WHITE);
  tela.drawLine(x, y + 6, x + 2, y + 8, SSD1306_WHITE);
  tela.drawLine(x + 2, y + 8, x + 4, y + 9, SSD1306_WHITE);
  tela.drawLine(x + 4, y + 9, x + 6, y + 8, SSD1306_WHITE);
  tela.drawLine(x + 6, y + 8, x + 8, y + 6, SSD1306_WHITE);
  tela.drawLine(x + 8, y + 6, x + 8, y + 4, SSD1306_WHITE);
  tela.drawLine(x + 4, y + 9, x + 4, y + 11, SSD1306_WHITE);
  if (!ativo) tela.drawLine(x - 1, y, x + 9, y + 10, SSD1306_WHITE);
}

void desenharIconeSom(int x, int y, bool ativo) {
  tela.drawLine(x, y + 4, x + 3, y + 4, SSD1306_WHITE);
  tela.drawLine(x + 3, y + 4, x + 6, y + 1, SSD1306_WHITE);
  tela.drawLine(x + 6, y + 1, x + 6, y + 10, SSD1306_WHITE);
  tela.drawLine(x + 6, y + 10, x + 3, y + 7, SSD1306_WHITE);
  tela.drawLine(x + 3, y + 7, x, y + 7, SSD1306_WHITE);
  tela.drawLine(x, y + 7, x, y + 4, SSD1306_WHITE);
  if (ativo) {
    tela.drawLine(x + 8, y + 3, x + 9, y + 5, SSD1306_WHITE);
    tela.drawLine(x + 9, y + 5, x + 8, y + 8, SSD1306_WHITE);
  } else {
    tela.drawLine(x - 1, y, x + 9, y + 10, SSD1306_WHITE);
  }
}

void desenharOLED() {
  if (!telaDisponivel || !telaOLEDAtiva) return;

  tela.clearDisplay();
  tela.setTextSize(1);
  tela.setTextColor(SSD1306_WHITE);
  tela.setCursor(0, 0);
  tela.println("TecBot");
  desenharIconeWifi(77, 0, WiFi.status() == WL_CONNECTED);
  desenharIconeServidor(90, 0, estadoServidorOLED == "conectado");
  desenharIconeMicrofone(104, 0, microfoneDisponivel && microfoneAtivo);
  desenharIconeSom(118, 0, somAtivo);

  if (aberturaAtiva && millis() - inicioAbertura < DURACAO_ABERTURA_MS) {
    tela.setTextSize(2);
    tela.setCursor(22, 12);
    tela.print("TecBot");
    tela.setTextSize(1);
    tela.setCursor(0, 32);
    tela.print(normalizarTextoOLED(textoInicialOLED));
    tela.setCursor(0, 43);
    tela.print("Wi-Fi: ");
    tela.print(WiFi.status() == WL_CONNECTED ? "conectado" : "aguardando");
    tela.setCursor(0, 54);
    tela.print("Servidor: ");
    tela.print(estadoServidorOLED);
  } else if (capturandoAudio ||
             estadoAudioExibido == AUDIO_AGUARDANDO) {
    tela.setCursor(0, 13);
    tela.print(capturandoAudio ? "Escutando voce..." : "Fale comigo");
    tela.drawCircle(64, 43, 18, SSD1306_WHITE);
    tela.fillCircle(58, 39, 2, SSD1306_WHITE);
    tela.fillCircle(70, 39, 2, SSD1306_WHITE);
    tela.drawLine(60, 49, 64, 51, SSD1306_WHITE);
    tela.drawLine(64, 51, 68, 49, SSD1306_WHITE);
    tela.drawCircle(42, 43, 5, SSD1306_WHITE);
    tela.drawCircle(86, 43, 5, SSD1306_WHITE);
  } else if (estadoAudioExibido == AUDIO_REPRODUZINDO) {
    tela.setCursor(0, 13);
    tela.print("TecBot falando...");
    tela.drawCircle(64, 43, 18, SSD1306_WHITE);
    tela.fillCircle(58, 39, 2, SSD1306_WHITE);
    tela.fillCircle(70, 39, 2, SSD1306_WHITE);
    if (animacaoBocaAberta) {
      tela.fillRoundRect(60, 46, 8, 10, 3, SSD1306_WHITE);
      tela.fillRoundRect(62, 48, 4, 6, 2, SSD1306_BLACK);
    } else {
      tela.drawLine(59, 49, 64, 52, SSD1306_WHITE);
      tela.drawLine(64, 52, 69, 49, SSD1306_WHITE);
    }
  } else if (modoHorarioAtivo) {
    tela.setTextSize(2);
    tela.setCursor(14, 14);
    tela.print(formatarHoraAtual());
    tela.setTextSize(1);
    tela.setCursor(0, 40);
    tela.print(formatarDataAtual());
    tela.setCursor(0, 52);
    tela.print(horaSincronizada() ? "hora do servidor" : "sincronizando...");
  } else {
    tela.setCursor(0, 13);
    tela.print("Servidor: ");
    tela.print(estadoServidorOLED);
    size_t proximoDeslocamento = deslocamentoMensagemOLED;
    tela.setCursor(0, 24);
    for (int i = 0; i < LINHAS_MENSAGEM_OLED; i++) {
      String linha = proximaLinhaOLED(proximoDeslocamento);
      if (linha.length() == 0 && proximoDeslocamento >= mensagemOLED.length()) {
        break;
      }
      tela.println(linha);
    }
    existeProximaPaginaOLED = proximoDeslocamento < mensagemOLED.length();
  }
  tela.display();
}

void mostrarMensagemOLED(const String& texto) {
  mensagemOLED = normalizarTextoOLED(texto);
  deslocamentoMensagemOLED = 0;
  ultimaPaginaOLED = millis();
  desenharOLED();
}

void definirEstadoServidorOLED(const String& novoEstado) {
  if (estadoServidorOLED == novoEstado) return;
  estadoServidorOLED = novoEstado;
  desenharOLED();
}

void definirEstadoAudio(EstadoAudio novoEstado) {
  estadoAudio = novoEstado;
}

void publicarTranscricaoAudio(const String& texto) {
  texto.toCharArray(transcricaoAudio, sizeof(transcricaoAudio));
  definirEstadoAudio(AUDIO_MOSTRANDO_TRANSCRICAO);
}

void atualizarTelaAudio() {
  EstadoAudio estadoAtual = estadoAudio;

  if (estadoAtual != estadoAudioExibido) {
    estadoAudioExibido = estadoAtual;
    switch (estadoAtual) {
      case AUDIO_AGUARDANDO:
        mostrarMensagemOLED("TecBot pronto! Fale agora");
        break;
      case AUDIO_GRAVANDO:
        mostrarMensagemOLED("Gravando audio...");
        break;
      case AUDIO_ENVIANDO:
        mostrarMensagemOLED("Enviando audio...");
        break;
      case AUDIO_PROCESSANDO:
        mostrarMensagemOLED("Processando voz...");
        break;
      case AUDIO_MOSTRANDO_TRANSCRICAO:
        mostrarMensagemOLED(String("TecBot: ") + String(transcricaoAudio));
        break;
      case AUDIO_REPRODUZINDO:
        desenharOLED();
        break;
      case AUDIO_MIC_DESATIVADO:
        mostrarMensagemOLED("Microfone desativado");
        break;
      case AUDIO_SOM_DESATIVADO:
        mostrarMensagemOLED("Som desativado");
        fimMensagemAudio = millis() + 1800;
        break;
      case AUDIO_CONCLUIDO:
        mostrarMensagemOLED("Reproducao concluida!");
        fimMensagemAudio = millis() + 1800;
        break;
      case AUDIO_ERRO_CONEXAO:
        mostrarMensagemOLED("Erro de conexao!");
        fimMensagemAudio = millis() + 2500;
        break;
      case AUDIO_ERRO_MICROFONE:
        mostrarMensagemOLED("Erro no microfone!");
        break;
      case AUDIO_ERRO_AUDIO:
        mostrarMensagemOLED("Erro no audio!");
        fimMensagemAudio = millis() + 2500;
        break;
    }
  }

  if ((estadoAtual == AUDIO_CONCLUIDO ||
       estadoAtual == AUDIO_SOM_DESATIVADO ||
       estadoAtual == AUDIO_ERRO_CONEXAO ||
       estadoAtual == AUDIO_ERRO_AUDIO) &&
      static_cast<long>(millis() - fimMensagemAudio) >= 0) {
    definirEstadoAudio(AUDIO_AGUARDANDO);
  }
}

void iniciarAmplificador() {
  if (amplificadorDisponivel) return;

  i2s_config_t configuracao = {};
  configuracao.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  configuracao.sample_rate = 22050;
  configuracao.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  configuracao.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  configuracao.communication_format = I2S_COMM_FORMAT_I2S;
  configuracao.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  configuracao.dma_buf_count = 8;
  configuracao.dma_buf_len = 256;
  configuracao.use_apll = false;
  configuracao.tx_desc_auto_clear = true;
  configuracao.fixed_mclk = 0;

  esp_err_t resultado = i2s_driver_install(
    PORTA_I2S_AMPLIFICADOR, &configuracao, 0, nullptr);
  if (resultado != ESP_OK) {
    Serial.printf("Falha ao iniciar I2S do amplificador: %d\n", resultado);
    return;
  }

  i2s_pin_config_t pinos = {};
  pinos.bck_io_num = PINO_AMPLIFICADOR_BCLK;
  pinos.ws_io_num = PINO_AMPLIFICADOR_LRC;
  pinos.data_out_num = PINO_AMPLIFICADOR_DIN;
  pinos.data_in_num = I2S_PIN_NO_CHANGE;
  pinos.mck_io_num = I2S_PIN_NO_CHANGE;
  resultado = i2s_set_pin(PORTA_I2S_AMPLIFICADOR, &pinos);
  if (resultado != ESP_OK) {
    i2s_driver_uninstall(PORTA_I2S_AMPLIFICADOR);
    Serial.printf("Falha ao configurar os pinos do amplificador: %d\n", resultado);
    return;
  }

  i2s_zero_dma_buffer(PORTA_I2S_AMPLIFICADOR);
  amplificadorDisponivel = true;
  Serial.println("Amplificador MAX98357A pronto.");
}

void iniciarMicrofone() {
  microfoneDisponivel = false;
  i2s_config_t configuracao = {};
  configuracao.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  configuracao.sample_rate = TAXA_AMOSTRAGEM_AUDIO;
  configuracao.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  configuracao.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  configuracao.communication_format = I2S_COMM_FORMAT_I2S;
  configuracao.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  configuracao.dma_buf_count = 8;
  configuracao.dma_buf_len = AMOSTRAS_BLOCO_AUDIO;
  configuracao.use_apll = false;
  configuracao.tx_desc_auto_clear = false;
  configuracao.fixed_mclk = 0;

  esp_err_t resultado = i2s_driver_install(
    PORTA_I2S_MICROFONE, &configuracao, 0, nullptr);
  if (resultado != ESP_OK) {
    Serial.printf("Falha ao iniciar I2S: %d\n", resultado);
    definirEstadoAudio(AUDIO_ERRO_MICROFONE);
    return;
  }

  i2s_pin_config_t pinos = {};
  pinos.bck_io_num = PINO_MICROFONE_BCLK;
  pinos.ws_io_num = PINO_MICROFONE_WS;
  pinos.data_out_num = I2S_PIN_NO_CHANGE;
  pinos.data_in_num = PINO_MICROFONE_DOUT;
  pinos.mck_io_num = I2S_PIN_NO_CHANGE;
  resultado = i2s_set_pin(PORTA_I2S_MICROFONE, &pinos);
  if (resultado != ESP_OK) {
    i2s_driver_uninstall(PORTA_I2S_MICROFONE);
    Serial.printf("Falha ao configurar os pinos do microfone: %d\n", resultado);
    definirEstadoAudio(AUDIO_ERRO_MICROFONE);
    return;
  }

  i2s_zero_dma_buffer(PORTA_I2S_MICROFONE);
  microfoneDisponivel = true;
  Serial.printf(
    "Microfone INMP441 pronto. Escuta continua ativada. Heap livre=%u bytes; watermark da pilha=%u.\n",
    static_cast<unsigned>(ESP.getFreeHeap()),
    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void gravarCabecalhoWav(size_t tamanhoAudio) {
  memcpy(bufferAudioWav, "RIFF", 4);
  uint32_t tamanhoArquivo = tamanhoAudio + TAMANHO_CABECALHO_WAV_ADPCM - 8;
  memcpy(bufferAudioWav + 4, &tamanhoArquivo, sizeof(tamanhoArquivo));
  memcpy(bufferAudioWav + 8, "WAVEfmt ", 8);
  uint32_t tamanhoFormato = 20;
  memcpy(bufferAudioWav + 16, &tamanhoFormato, sizeof(tamanhoFormato));
  uint16_t formatoImaAdpcm = 0x0011;
  memcpy(bufferAudioWav + 20, &formatoImaAdpcm, sizeof(formatoImaAdpcm));
  uint16_t canais = 1;
  memcpy(bufferAudioWav + 22, &canais, sizeof(canais));
  uint32_t taxa = TAXA_AMOSTRAGEM_AUDIO;
  memcpy(bufferAudioWav + 24, &taxa, sizeof(taxa));
  uint32_t bytesPorSegundo =
    TAXA_AMOSTRAGEM_AUDIO * ALINHAMENTO_BLOCO_ADPCM /
    AMOSTRAS_BLOCO_ADPCM;
  memcpy(bufferAudioWav + 28, &bytesPorSegundo, sizeof(bytesPorSegundo));
  uint16_t alinhamento = ALINHAMENTO_BLOCO_ADPCM;
  memcpy(bufferAudioWav + 32, &alinhamento, sizeof(alinhamento));
  uint16_t bitsPorAmostra = 4;
  memcpy(bufferAudioWav + 34, &bitsPorAmostra, sizeof(bitsPorAmostra));
  uint16_t tamanhoExtra = 2;
  memcpy(bufferAudioWav + 36, &tamanhoExtra, sizeof(tamanhoExtra));
  uint16_t amostrasPorBloco = AMOSTRAS_BLOCO_ADPCM;
  memcpy(bufferAudioWav + 38, &amostrasPorBloco, sizeof(amostrasPorBloco));
  memcpy(bufferAudioWav + 40, "fact", 4);
  uint32_t tamanhoFact = 4;
  memcpy(bufferAudioWav + 44, &tamanhoFact, sizeof(tamanhoFact));
  uint32_t quantidadeAmostras = quantidadeAmostrasAudio;
  memcpy(bufferAudioWav + 48, &quantidadeAmostras, sizeof(quantidadeAmostras));
  memcpy(bufferAudioWav + 52, "data", 4);
  uint32_t tamanhoDados = tamanhoAudio;
  memcpy(bufferAudioWav + 56, &tamanhoDados, sizeof(tamanhoDados));
}

void adicionarAmostraAdpcm(int16_t amostra, bool contarAmostra) {
  if (amostrasBlocoAudio == 0) {
    size_t inicioBloco = TAMANHO_CABECALHO_WAV_ADPCM +
      blocosAudio * ALINHAMENTO_BLOCO_ADPCM;
    memcpy(bufferAudioWav + inicioBloco, &amostra, sizeof(amostra));
    bufferAudioWav[inicioBloco + 2] = 0;
    bufferAudioWav[inicioBloco + 3] = 0;
    preditorAdpcm = amostra;
    indiceAdpcm = 0;
    amostrasBlocoAudio = 1;
    if (contarAmostra) quantidadeAmostrasAudio++;
    return;
  }

  static const int16_t TABELA_PASSOS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767
  };
  static const int8_t TABELA_INDICE[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
  };

  int32_t diferenca = static_cast<int32_t>(amostra) - preditorAdpcm;
  uint8_t codigo = 0;
  if (diferenca < 0) {
    codigo = 8;
    diferenca = -diferenca;
  }
  int32_t passo = TABELA_PASSOS[indiceAdpcm];
  int32_t delta = passo >> 3;
  if (diferenca >= passo) {
    codigo |= 4;
    diferenca -= passo;
    delta += passo;
  }
  if (diferenca >= (passo >> 1)) {
    codigo |= 2;
    diferenca -= passo >> 1;
    delta += passo >> 1;
  }
  if (diferenca >= (passo >> 2)) {
    codigo |= 1;
    delta += passo >> 2;
  }

  preditorAdpcm += (codigo & 8) ? -delta : delta;
  preditorAdpcm = constrain(preditorAdpcm, -32768, 32767);
  indiceAdpcm += TABELA_INDICE[codigo];
  indiceAdpcm = constrain(indiceAdpcm, 0, 88);

  size_t indiceNibbles = amostrasBlocoAudio - 1;
  size_t indiceByte = TAMANHO_CABECALHO_WAV_ADPCM +
    blocosAudio * ALINHAMENTO_BLOCO_ADPCM + 4 + indiceNibbles / 2;
  if (indiceNibbles % 2 == 0) {
    bufferAudioWav[indiceByte] = codigo;
  } else {
    bufferAudioWav[indiceByte] |= codigo << 4;
  }
  amostrasBlocoAudio++;
  if (contarAmostra) quantidadeAmostrasAudio++;
  if (amostrasBlocoAudio == AMOSTRAS_BLOCO_ADPCM) {
    blocosAudio++;
    amostrasBlocoAudio = 0;
  }
}

void finalizarAudioAdpcm() {
  if (amostrasBlocoAudio == 0) return;
  size_t amostrasRestantes = AMOSTRAS_BLOCO_ADPCM - amostrasBlocoAudio;
  for (size_t i = 0; i < amostrasRestantes; i++) {
    adicionarAmostraAdpcm(preditorAdpcm, false);
  }
}

bool lerBytesAudio(WiFiClient* fluxo, uint8_t* destino, size_t quantidade) {
  return fluxo && fluxo->readBytes(destino, quantidade) == quantidade;
}

bool pularBytesAudio(WiFiClient* fluxo, uint32_t quantidade) {
  uint8_t descarte[64];
  while (quantidade > 0) {
    size_t bloco = quantidade < sizeof(descarte)
      ? quantidade : sizeof(descarte);
    if (!lerBytesAudio(fluxo, descarte, bloco)) return false;
    quantidade -= bloco;
  }
  return true;
}

uint16_t lerUint16LE(const uint8_t* dados) {
  return static_cast<uint16_t>(dados[0]) |
    static_cast<uint16_t>(dados[1]) << 8;
}

uint32_t lerUint32LE(const uint8_t* dados) {
  return static_cast<uint32_t>(dados[0]) |
    static_cast<uint32_t>(dados[1]) << 8 |
    static_cast<uint32_t>(dados[2]) << 16 |
    static_cast<uint32_t>(dados[3]) << 24;
}

int16_t converterAmostraPCM(const uint8_t* dados, uint16_t bits) {
  if (bits == 8) {
    return static_cast<int16_t>(
      (static_cast<int16_t>(dados[0]) - 128) * 256);
  }
  if (bits == 16) {
    return static_cast<int16_t>(lerUint16LE(dados));
  }
  if (bits == 24) {
    uint32_t valor = static_cast<uint32_t>(dados[0]) |
      static_cast<uint32_t>(dados[1]) << 8 |
      static_cast<uint32_t>(dados[2]) << 16;
    int32_t amostra = (valor & 0x800000)
      ? static_cast<int32_t>(valor | 0xFF000000)
      : static_cast<int32_t>(valor);
    return static_cast<int16_t>(amostra >> 8);
  }
  int32_t amostra = static_cast<int32_t>(lerUint32LE(dados));
  return static_cast<int16_t>(amostra >> 16);
}

int16_t ajustarVolume(int16_t amostra) {
  return static_cast<int16_t>(
    static_cast<int32_t>(amostra) * volumeSom / 100);
}

bool reproduzirWavRecebido(HTTPClient& http) {
  WiFiClient* fluxo = http.getStreamPtr();
  int tamanhoResposta = http.getSize();
  if (!fluxo || tamanhoResposta < 44) {
    Serial.printf(
      "Resposta WAV invalida: fluxo=%s, tamanho=%d\n",
      fluxo ? "disponivel" : "indisponivel", tamanhoResposta);
    return false;
  }
  fluxo->setTimeout(5000);

  uint8_t cabecalho[12];
  if (!lerBytesAudio(fluxo, cabecalho, sizeof(cabecalho)) ||
      memcmp(cabecalho, "RIFF", 4) != 0 ||
      memcmp(cabecalho + 8, "WAVE", 4) != 0) {
    Serial.println("Resposta de voz nao contem um cabecalho WAV RIFF valido.");
    return false;
  }

  uint32_t restante = static_cast<uint32_t>(tamanhoResposta) - 12;
  uint16_t formato = 0;
  uint16_t canais = 0;
  uint16_t bitsPorAmostra = 0;
  uint16_t alinhamento = 0;
  uint32_t taxa = 0;
  bool formatoLido = false;
  uint32_t tamanhoDados = 0;

  while (restante >= 8) {
    uint8_t cabecalhoBloco[8];
    if (!lerBytesAudio(fluxo, cabecalhoBloco, sizeof(cabecalhoBloco))) {
      Serial.println("Falha ao ler bloco do WAV de resposta.");
      return false;
    }
    restante -= sizeof(cabecalhoBloco);
    uint32_t tamanhoBloco = lerUint32LE(cabecalhoBloco + 4);
    if (tamanhoBloco + (tamanhoBloco & 1) > restante) {
      Serial.println("Bloco do WAV excede o tamanho recebido.");
      return false;
    }

    if (memcmp(cabecalhoBloco, "fmt ", 4) == 0) {
      if (tamanhoBloco < 16) {
        Serial.println("Bloco de formato do WAV e menor que o esperado.");
        return false;
      }
      uint8_t camposFormato[16];
      if (!lerBytesAudio(fluxo, camposFormato, sizeof(camposFormato))) {
        Serial.println("Falha ao ler o formato do WAV.");
        return false;
      }
      formato = lerUint16LE(camposFormato);
      canais = lerUint16LE(camposFormato + 2);
      taxa = lerUint32LE(camposFormato + 4);
      alinhamento = lerUint16LE(camposFormato + 12);
      bitsPorAmostra = lerUint16LE(camposFormato + 14);
      if (!pularBytesAudio(fluxo, tamanhoBloco - sizeof(camposFormato))) {
        Serial.println("Falha ao ler dados extras do formato WAV.");
        return false;
      }
      restante -= tamanhoBloco;
      if ((tamanhoBloco & 1) != 0) {
        if (!pularBytesAudio(fluxo, 1)) {
          Serial.println("Falha ao ler alinhamento do formato WAV.");
          return false;
        }
        restante--;
      }
      formatoLido = true;
    } else if (memcmp(cabecalhoBloco, "data", 4) == 0) {
      if (!formatoLido) {
        Serial.println("WAV de resposta nao tem formato antes dos dados.");
        return false;
      }
      tamanhoDados = tamanhoBloco;
      break;
    } else {
      if (!pularBytesAudio(fluxo, tamanhoBloco)) {
        Serial.println("Falha ao pular bloco extra do WAV.");
        return false;
      }
      restante -= tamanhoBloco;
      if ((tamanhoBloco & 1) != 0) {
        if (!pularBytesAudio(fluxo, 1)) {
          Serial.println("Falha ao ler alinhamento de bloco extra do WAV.");
          return false;
        }
        restante--;
      }
    }
  }

  if (formato != 1 || (canais != 1 && canais != 2) ||
      (bitsPorAmostra != 8 && bitsPorAmostra != 16 &&
       bitsPorAmostra != 24 && bitsPorAmostra != 32) ||
      taxa < 8000 || taxa > 48000) {
    Serial.printf(
      "Formato WAV nao suportado: formato=%u, canais=%u, taxa=%lu, bits=%u\n",
      formato, canais, static_cast<unsigned long>(taxa), bitsPorAmostra);
    return false;
  }

  uint16_t bytesPorAmostra = bitsPorAmostra / 8;
  uint16_t bytesPorQuadro = bytesPorAmostra * canais;
  if (!tamanhoDados || alinhamento != bytesPorQuadro ||
      tamanhoDados % bytesPorQuadro != 0) {
    Serial.printf(
      "Dados WAV invalidos: bytes=%lu, alinhamento=%u, quadro=%u\n",
      static_cast<unsigned long>(tamanhoDados), alinhamento, bytesPorQuadro);
    return false;
  }
  iniciarAmplificador();
  if (!amplificadorDisponivel) {
    Serial.println("Amplificador I2S indisponivel.");
    return false;
  }
  esp_err_t resultadoClock = i2s_set_clk(
    PORTA_I2S_AMPLIFICADOR, taxa,
    I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  if (resultadoClock != ESP_OK) {
    Serial.printf("Falha ao ajustar clock I2S do amplificador: %d\n",
      resultadoClock);
    return false;
  }

  const size_t QUADROS_POR_BLOCO = 128;
  uint8_t amostrasEntrada[QUADROS_POR_BLOCO * 2 * 4];
  int16_t amostrasSaida[QUADROS_POR_BLOCO * 2];
  uint32_t quadrosRestantes = tamanhoDados / bytesPorQuadro;
  while (quadrosRestantes > 0) {
    size_t quadros = quadrosRestantes < QUADROS_POR_BLOCO
      ? quadrosRestantes : QUADROS_POR_BLOCO;
    size_t bytesEntrada = quadros * bytesPorQuadro;
    if (!lerBytesAudio(fluxo, amostrasEntrada, bytesEntrada)) {
      Serial.printf(
        "Leitura WAV incompleta: esperados %u bytes de amostras.\n",
        static_cast<unsigned>(bytesEntrada));
      return false;
    }

    for (size_t i = 0; i < quadros; i++) {
      const uint8_t* quadro = amostrasEntrada + i * bytesPorQuadro;
      int16_t esquerda = converterAmostraPCM(quadro, bitsPorAmostra);
      int16_t direita = canais == 2
        ? converterAmostraPCM(quadro + bytesPorAmostra, bitsPorAmostra)
        : esquerda;
      amostrasSaida[i * 2] = ajustarVolume(esquerda);
      amostrasSaida[i * 2 + 1] = ajustarVolume(direita);
    }

    size_t bytesEscritos = 0;
    size_t bytesSaida = quadros * 2 * sizeof(int16_t);
    esp_err_t resultadoEscrita = i2s_write(
      PORTA_I2S_AMPLIFICADOR, amostrasSaida, bytesSaida,
      &bytesEscritos, pdMS_TO_TICKS(1000));
    if (resultadoEscrita != ESP_OK || bytesEscritos != bytesSaida) {
      Serial.printf(
        "Falha na saida I2S: erro=%d, enviados=%u de %u bytes.\n",
        resultadoEscrita, static_cast<unsigned>(bytesEscritos),
        static_cast<unsigned>(bytesSaida));
      return false;
    }
    quadrosRestantes -= quadros;
    if (millis() - ultimaAnimacaoRosto >= INTERVALO_ANIMACAO_ROSTO_MS) {
      ultimaAnimacaoRosto = millis();
      animacaoBocaAberta = !animacaoBocaAberta;
      desenharOLED();
    }
  }
  delay(100);
  return true;
}

void finalizarTransacaoAudio(EstadoAudio estadoFinal) {
  definirEstadoAudio(estadoFinal);
  audioTransacaoAtiva = false;
  tamanhoAudioCapturado = 0;
}

void processarTransacaoAudio() {
  const size_t TAMANHO_MINIMO_AMOSTRAS = TAXA_AMOSTRAGEM_AUDIO / 5;
  if (quantidadeAmostrasAudio < TAMANHO_MINIMO_AMOSTRAS ||
      WiFi.status() != WL_CONNECTED ||
      urlServidor.length() == 0 ||
      ESP32_DEVICE_TOKEN.length() == 0) {
    finalizarTransacaoAudio(AUDIO_ERRO_CONEXAO);
    return;
  }

  tamanhoAudioCapturado = blocosAudio * ALINHAMENTO_BLOCO_ADPCM;
  gravarCabecalhoWav(tamanhoAudioCapturado);
  WiFiClient cliente;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(65000);
  if (!http.begin(cliente, urlServidor + "/api/device/voice-command")) {
    finalizarTransacaoAudio(AUDIO_ERRO_CONEXAO);
    return;
  }
  http.addHeader("Authorization", String("Bearer ") + ESP32_DEVICE_TOKEN);
  http.addHeader("Content-Type", "audio/wav");
  int codigoHTTP = http.POST(
    bufferAudioWav, tamanhoAudioCapturado + TAMANHO_CABECALHO_WAV_ADPCM);
  String corpoErro = codigoHTTP > 0 && codigoHTTP != HTTP_CODE_OK &&
      codigoHTTP != 204
    ? http.getString() : "";
  if (codigoHTTP == 204) {
    http.end();
    finalizarTransacaoAudio(AUDIO_AGUARDANDO);
    return;
  }
  uint64_t idMensagem = 0;
  if (codigoHTTP == HTTP_CODE_OK) {
    DynamicJsonDocument resultado(256);
    DeserializationError erro =
      deserializeJson(resultado, http.getString());
    if (!erro && resultado["id"].is<uint64_t>() &&
        resultado["id"].as<uint64_t>() > 0) {
      idMensagem = resultado["id"].as<uint64_t>();
    }
  }
  http.end();
  if (codigoHTTP != HTTP_CODE_OK || idMensagem == 0) {
    Serial.printf("Falha ao enviar audio ao servidor: HTTP %d\n", codigoHTTP);
    if (corpoErro.length() > 0) Serial.println(corpoErro);
    finalizarTransacaoAudio(AUDIO_ERRO_CONEXAO);
    return;
  }

  definirEstadoAudio(AUDIO_PROCESSANDO);
  WiFiClient clienteMensagem;
  HTTPClient httpMensagem;
  httpMensagem.setConnectTimeout(5000);
  httpMensagem.setTimeout(5000);
  if (!httpMensagem.begin(
      clienteMensagem, urlServidor + "/api/device/message")) {
    finalizarTransacaoAudio(AUDIO_ERRO_CONEXAO);
    return;
  }
  httpMensagem.addHeader(
    "Authorization", String("Bearer ") + ESP32_DEVICE_TOKEN);
  int codigoMensagem = httpMensagem.GET();
  String textoReconhecido;
  if (codigoMensagem == HTTP_CODE_OK) {
    DynamicJsonDocument mensagem(16384);
    DeserializationError erro =
      deserializeJson(mensagem, httpMensagem.getString());
    if (!erro && mensagem["id"].is<uint64_t>() &&
        mensagem["id"].as<uint64_t>() == idMensagem &&
        mensagem["texto"].is<const char*>()) {
      textoReconhecido = mensagem["texto"].as<String>();
    }
  }
  httpMensagem.end();
  if (codigoMensagem != HTTP_CODE_OK || textoReconhecido.length() == 0) {
    Serial.printf("Falha ao obter transcricao: HTTP %d\n", codigoMensagem);
    finalizarTransacaoAudio(AUDIO_ERRO_CONEXAO);
    return;
  }

  publicarTranscricaoAudio(textoReconhecido);
  ultimoIdExibido = idMensagem;
  atualizarTelaAudio();
  if (!somAtivo) {
    finalizarTransacaoAudio(AUDIO_SOM_DESATIVADO);
    return;
  }
  delay(1500);

  WiFiClient clienteFala;
  HTTPClient httpFala;
  httpFala.setConnectTimeout(5000);
  httpFala.setTimeout(65000);
  if (!httpFala.begin(clienteFala, urlServidor + "/api/device/speech")) {
    Serial.println("Nao foi possivel iniciar a requisicao de voz ao servidor.");
    finalizarTransacaoAudio(AUDIO_ERRO_CONEXAO);
    return;
  }
  StaticJsonDocument<32> solicitacaoFala;
  solicitacaoFala["id"] = idMensagem;
  String corpoFala;
  serializeJson(solicitacaoFala, corpoFala);
  httpFala.addHeader(
    "Authorization", String("Bearer ") + ESP32_DEVICE_TOKEN);
  httpFala.addHeader("Content-Type", "application/json");
  int codigoFala = httpFala.POST(corpoFala);
  if (codigoFala != HTTP_CODE_OK) {
    Serial.printf("Falha ao obter audio de resposta: HTTP %d\n", codigoFala);
    if (codigoFala > 0) {
      Serial.println(httpFala.getString());
    }
    httpFala.end();
    finalizarTransacaoAudio(
      codigoFala < 0 ? AUDIO_ERRO_CONEXAO : AUDIO_ERRO_AUDIO);
    return;
  }

  definirEstadoAudio(AUDIO_REPRODUZINDO);
  ultimaAnimacaoRosto = millis();
  animacaoBocaAberta = false;
  atualizarTelaAudio();
  bool audioReproduzido = reproduzirWavRecebido(httpFala);
  httpFala.end();
  capturaMicrofoneBloqueadaAte = millis() + 500;
  pausaMicrofonePosAudioAtiva = true;
  finalizarTransacaoAudio(
    audioReproduzido ? AUDIO_CONCLUIDO : AUDIO_ERRO_AUDIO);
}

void processarAudioMicrofone() {
  if (!microfoneAtivo || !microfoneDisponivel || audioTransacaoAtiva) {
    if (!capturandoAudio) blocosVozConsecutivosAudio = 0;
    return;
  }
  if (pausaMicrofonePosAudioAtiva) {
    if (static_cast<long>(millis() - capturaMicrofoneBloqueadaAte) < 0) return;
    pausaMicrofonePosAudioAtiva = false;
  }
  if (!capturandoAudio && estadoAudio != AUDIO_AGUARDANDO) return;

  static bool primeiraLeituraRegistrada = false;
  static uint32_t leiturasDesdeUltimoLog = 0;
  if (!primeiraLeituraRegistrada) {
    primeiraLeituraRegistrada = true;
    Serial.println("Iniciando primeira leitura I2S do microfone.");
  }

  size_t bytesLidos = 0;
  esp_err_t resultado = i2s_read(
    PORTA_I2S_MICROFONE, bufferAmostrasMicrofone,
    sizeof(bufferAmostrasMicrofone), &bytesLidos,
    pdMS_TO_TICKS(20));
  if (resultado != ESP_OK || bytesLidos < sizeof(int32_t) ||
      bytesLidos > sizeof(bufferAmostrasMicrofone)) {
    if (resultado != ESP_OK) {
      Serial.printf("Falha na leitura I2S do microfone: erro=%d.\n", resultado);
    }
    return;
  }

  size_t quantidade = bytesLidos / sizeof(int32_t);
  if (++leiturasDesdeUltimoLog >= 100) {
    leiturasDesdeUltimoLog = 0;
    Serial.printf(
      "Leitura I2S ativa: bytes=%u; watermark da pilha=%u.\n",
      static_cast<unsigned>(bytesLidos),
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }
  uint64_t somaQuadrados = 0;
  for (size_t i = 0; i < quantidade; i++) {
    int32_t amostra = bufferAmostrasMicrofone[i] >> 16;
    amostra = constrain(amostra, -32768, 32767);
    somaQuadrados += static_cast<int64_t>(amostra) * amostra;
  }
  uint32_t rms = sqrt(static_cast<double>(somaQuadrados) / quantidade);
  unsigned long agora = millis();
  if (!capturandoAudio) {
    if (rms >= LIMIAR_AUDIO_RMS && WiFi.status() == WL_CONNECTED) {
      if (blocosVozConsecutivosAudio < BLOCOS_VOZ_CONSECUTIVOS_MINIMOS) {
        blocosVozConsecutivosAudio++;
      }
      if (blocosVozConsecutivosAudio >= BLOCOS_VOZ_CONSECUTIVOS_MINIMOS) {
        capturandoAudio = true;
        falaEmAndamento = false;
        definirEstadoAudio(AUDIO_GRAVANDO);
        tamanhoAudioCapturado = 0;
        quantidadeAmostrasAudio = 0;
        amostrasVozAudio = 0;
        blocosAudio = 0;
        amostrasBlocoAudio = 0;
        inicioSilencioAudio = 0;
        desenharOLED();
      }
    } else {
      blocosVozConsecutivosAudio = 0;
    }
  }

  if (!capturandoAudio) return;

  for (size_t i = 0; i < quantidade; i++) {
    if (quantidadeAmostrasAudio >= MAXIMO_AMOSTRAS_AUDIO) {
      break;
    }
    int32_t amostra = constrain(
      bufferAmostrasMicrofone[i] >> 16, -32768, 32767);
    adicionarAmostraAdpcm(static_cast<int16_t>(amostra), true);
  }

  if (rms < LIMIAR_AUDIO_RMS) {
    if (inicioSilencioAudio == 0) inicioSilencioAudio = agora;
  } else {
    inicioSilencioAudio = 0;
    amostrasVozAudio += quantidade;
  }

  bool silencioDetectado = inicioSilencioAudio != 0 &&
    agora - inicioSilencioAudio >= DURACAO_SILENCIO_AUDIO_MS;
  bool duracaoMaxima = quantidadeAmostrasAudio >= MAXIMO_AMOSTRAS_AUDIO;
  if (silencioDetectado || duracaoMaxima) {
    capturandoAudio = false;
    blocosVozConsecutivosAudio = 0;
    if (amostrasVozAudio <
        TAXA_AMOSTRAGEM_AUDIO * DURACAO_MINIMA_VOZ_AUDIO_MS / 1000) {
      quantidadeAmostrasAudio = 0;
      amostrasVozAudio = 0;
      blocosAudio = 0;
      amostrasBlocoAudio = 0;
      inicioSilencioAudio = 0;
      definirEstadoAudio(AUDIO_AGUARDANDO);
      atualizarTelaAudio();
      return;
    }
    finalizarAudioAdpcm();
    desenharOLED();
    audioTransacaoAtiva = true;
    audioTransacaoPendente = true;
    definirEstadoAudio(AUDIO_ENVIANDO);
    atualizarTelaAudio();
  }
}


void iniciarConexao(String ssid, String senha) {
  ssidTentativa = ssid;
  senhaTentativa = senha;
  ssidConfigurado = ssid;

  if (!WiFi.mode(WIFI_AP_STA)) {
    tentandoConectar = false;
    Serial.println("Falha ao habilitar Wi-Fi cliente; AP de configuracao mantido.");
    definirEstadoServidorOLED("AP ativo");
    mostrarMensagemOLED("Configure o Wi-Fi em 192.168.4.1");
    return;
  }

  inicioConexao = millis();
  tentandoConectar = true;

  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(ssidTentativa.c_str(), senhaTentativa.c_str());

  Serial.println("Tentando conectar ao Wi-Fi selecionado.");
}

void consultarMensagemServidor() {
  if (WiFi.status() != WL_CONNECTED) {
    definirEstadoServidorOLED("sem Wi-Fi");
    return;
  }
  if (urlServidor.length() == 0) {
    definirEstadoServidorOLED("sem URL");
    return;
  }
  if (ESP32_DEVICE_TOKEN.length() == 0) {
    definirEstadoServidorOLED("sem token");
    return;
  }
  if (millis() - ultimaConsultaServidor < INTERVALO_CONSULTA_SERVIDOR) {
    return;
  }
  ultimaConsultaServidor = millis();

  WiFiClient cliente;
  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_SERVIDOR);
  http.setTimeout(TIMEOUT_SERVIDOR);
  String endpoint = urlServidor + "/api/device/message";

  if (!http.begin(cliente, endpoint)) {
    definirEstadoServidorOLED("offline");
    return;
  }
  http.addHeader("Authorization", String("Bearer ") + ESP32_DEVICE_TOKEN);

  int codigoHTTP = http.GET();
  if (codigoHTTP == HTTP_CODE_OK) {
    String corpo = http.getString();
    DynamicJsonDocument documento(16384);
    DeserializationError erroJSON = deserializeJson(documento, corpo);

    if (erroJSON || !documento["id"].is<uint64_t>() ||
        !documento["texto"].is<const char*>()) {
      definirEstadoServidorOLED("JSON inval.");
    } else {
      uint64_t id = documento["id"].as<uint64_t>();
      String texto = documento["texto"].as<String>();
      definirEstadoServidorOLED("conectado");
      if (id > 0 && id != ultimoIdExibido && texto.length() > 0) {
        ultimoIdExibido = id;
        mostrarMensagemOLED(texto);
        falaEmAndamento = true;
        inicioResposta = millis();
        ultimaAnimacaoRosto = millis();
        animacaoBocaAberta = false;
        desenharOLED();
      }
    }
  } else if (codigoHTTP == HTTP_CODE_UNAUTHORIZED) {
    definirEstadoServidorOLED("401 token");
  } else {
    definirEstadoServidorOLED("offline");
  }

  http.end();
}

void enviarHeartbeatServidor() {
  if (WiFi.status() != WL_CONNECTED || urlServidor.length() == 0 ||
      ESP32_DEVICE_TOKEN.length() == 0 ||
      audioTransacaoAtiva ||
      (ultimoHeartbeatServidor != 0 &&
       millis() - ultimoHeartbeatServidor < INTERVALO_HEARTBEAT_SERVIDOR)) {
    return;
  }
  ultimoHeartbeatServidor = millis();

  WiFiClient cliente;
  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_POST_SENSOR);
  http.setTimeout(TIMEOUT_POST_SENSOR);
  if (!http.begin(cliente, urlServidor + "/api/devices/heartbeat")) {
    return;
  }

  StaticJsonDocument<64> identificacao;
  identificacao["deviceId"] = ID_DISPOSITIVO;
  String corpo;
  serializeJson(identificacao, corpo);
  http.addHeader("Authorization", String("Bearer ") + ESP32_DEVICE_TOKEN);
  http.addHeader("Content-Type", "application/json");
  int codigoHTTP = http.POST(corpo);
  if (codigoHTTP == HTTP_CODE_OK) {
    StaticJsonDocument<256> resultado;
    if (!deserializeJson(resultado, http.getString()) &&
        resultado["receivedAt"].is<const char*>() &&
        horaServidorEpoch == 0) {
      time_t epoch = parsearIso8601(resultado["receivedAt"].as<String>());
      if (epoch > 0) {
        horaServidorEpoch = epoch;
        millisHoraServidor = millis();
      }
    }
  } else if (codigoHTTP == HTTP_CODE_UNAUTHORIZED) {
    definirEstadoServidorOLED("401 token");
  } else if (codigoHTTP == HTTP_CODE_BAD_REQUEST) {
    definirEstadoServidorOLED("ID invalido");
  }
  http.end();
}

float medirDistanciaHCSR04() {
  digitalWrite(PINO_HCSR04_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PINO_HCSR04_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_HCSR04_TRIG, LOW);

  unsigned long duracao = pulseIn(
    PINO_HCSR04_ECHO, HIGH, TIMEOUT_ECHO_HCSR04_US);
  if (duracao == 0) return 0;

  float distanciaCm = duracao * 0.0343f / 2.0f;
  if (distanciaCm < 2.0f || distanciaCm > 400.0f) return 0;
  return distanciaCm;
}

void enviarLeituraHCSR04(float distanciaCm) {
  if (WiFi.status() != WL_CONNECTED || urlServidor.length() == 0 ||
      ESP32_DEVICE_TOKEN.length() == 0 || audioTransacaoAtiva) {
    return;
  }

  WiFiClient cliente;
  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_POST_SENSOR);
  http.setTimeout(TIMEOUT_POST_SENSOR);
  if (!http.begin(cliente, urlServidor + "/api/devices/sensor-reading")) {
    return;
  }

  StaticJsonDocument<128> leitura;
  leitura["deviceId"] = ID_DISPOSITIVO;
  leitura["distanceCm"] = distanciaCm;
  String corpo;
  serializeJson(leitura, corpo);

  http.addHeader("Authorization", String("Bearer ") + ESP32_DEVICE_TOKEN);
  http.addHeader("Content-Type", "application/json");
  int codigoHTTP = http.POST(corpo);
  if (codigoHTTP == HTTP_CODE_ACCEPTED) {
    StaticJsonDocument<256> resultado;
    DeserializationError erro =
      deserializeJson(resultado, http.getString());
    if (!erro && resultado["triggered"] == true) {
      mostrarMensagemOLED(
        "Objeto detectado a " + String(distanciaCm, 1) + " cm");
    }
  } else if (codigoHTTP == HTTP_CODE_UNAUTHORIZED) {
    definirEstadoServidorOLED("401 token");
  }
  http.end();
}

String paginaProvisionamento() {
  String pagina = F(
    "<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta name=\"theme-color\" content=\"#101827\">"
    "<title>TecBot | Configuracao</title>"
    "<style>"
    "*{box-sizing:border-box}body{margin:0;min-height:100vh;padding:32px 16px;"
    "font-family:Arial,Helvetica,sans-serif;color:#e8eef8;background:"
    "radial-gradient(ellipse at 20% 0%,#1d3b58 0,transparent 42%),#101827}"
    ".wrap{width:min(100%,520px);margin:auto}.brand{display:flex;align-items:center;"
    "gap:12px;margin:0 0 20px}.logo{width:44px;height:44px;border-radius:14px;"
    "display:grid;place-items:center;background:#1fbb92;color:#081b1a;font-size:23px}"
    "h1{font-size:21px;margin:0}.subtitle{color:#9eacc0;margin:5px 0 0;font-size:14px}"
    ".card{padding:24px;border:1px solid #29384b;border-radius:20px;"
    "background:rgba(22,33,49,.94);box-shadow:0 18px 50px #0005}"
    ".section{font-size:14px;font-weight:700;color:#d7e2f1;margin:0 0 16px}"
    ".field{margin:0 0 17px}label{display:block;margin-bottom:7px;font-size:13px;"
    "font-weight:700;color:#bdc9d9}.hint{font-size:12px;color:#8595aa;margin-top:6px}"
    "input,select{width:100%;height:46px;padding:0 13px;border:1px solid #3b4b60;"
    "border-radius:11px;background:#111c2a;color:#edf4ff;font-size:15px;outline:none}"
    "input:focus,select:focus{border-color:#1fbb92;box-shadow:0 0 0 3px #1fbb9225}"
    ".row{display:flex;gap:8px}.row select{min-width:0;flex:1}"
    ".refresh{width:48px;height:46px;border:1px solid #3b4b60;border-radius:11px;"
    "color:#d8e6f4;background:#1b2a3c;font-size:20px;cursor:pointer}"
    ".status{min-height:18px;font-size:12px;color:#92a4b9;margin-top:7px}"
    ".status.error{color:#ff9b9b}.status.ok{color:#62d5b2}"
    ".manual{display:none;margin-top:10px}.submit{width:100%;height:48px;margin-top:4px;"
    "border:0;border-radius:12px;background:#1fbb92;color:#06251e;font-weight:800;"
    "font-size:15px;cursor:pointer}.submit:active{transform:translateY(1px)}"
    ".test-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:0 0 18px}"
    ".test-button{min-height:44px;border:1px solid #3b4b60;border-radius:11px;"
    "background:#1b2a3c;color:#e8eef8;font-weight:700;cursor:pointer}"
    ".test-button:disabled{opacity:.55;cursor:wait}.test-status{grid-column:1/-1;"
    "min-height:18px;margin:0;color:#92a4b9;font-size:12px;line-height:1.5}"
    ".test-status.error{color:#ff9b9b}.test-status.ok{color:#62d5b2}"
    ".audio-settings{display:grid;gap:10px;margin:0 0 18px;padding:14px;"
    "border:1px solid #2b3a4d;border-radius:12px;background:#111c2a}"
    ".audio-toggle{display:flex;align-items:center;justify-content:space-between;"
    "gap:12px;color:#d7e2f1;font-size:13px;font-weight:700}"
    ".audio-toggle input{width:20px;height:20px;accent-color:#1fbb92}"
    ".audio-toggle input:disabled{opacity:.6}"
    ".audio-settings .test-status{margin-top:2px}"
    ".divider{height:1px;background:#2b3a4d;margin:22px 0}"
    ".foot{text-align:center;color:#718198;font-size:12px;margin:16px 0 0}"
    "@media(max-width:420px){body{padding:20px 12px}.card{padding:19px}}"
    "</style></head><body><main class=\"wrap\">"
    "<header class=\"brand\"><div class=\"logo\" aria-hidden=\"true\">&#9696;</div>"
    "<div><h1>TecBot</h1><p class=\"subtitle\">Configuracao do dispositivo</p></div></header>"
    "<form class=\"card\" method=\"POST\" action=\"/provisionar\" id=\"config-form\">"
    "<p class=\"section\">Conexao Wi-Fi</p>"
    "<div class=\"field\"><label for=\"ssid\">Selecione sua rede</label>"
    "<div class=\"row\"><select id=\"ssid\" name=\"ssid\" required>"
    "<option value=\"\" selected>Buscando redes Wi-Fi...</option>"
    "</select><button class=\"refresh\" type=\"button\" id=\"refresh\" "
    "title=\"Buscar redes novamente\" aria-label=\"Buscar redes novamente\">&#8635;</button></div>"
    "<div class=\"status\" id=\"scan-status\" role=\"status\">Aguarde a busca por redes proximas.</div>"
    "<div class=\"manual\" id=\"manual-wrap\"><input id=\"manual-ssid\" maxlength=\"32\" "
    "placeholder=\"Digite o nome da rede Wi-Fi\" autocomplete=\"off\"></div></div>"
    "<div class=\"field\"><label for=\"senha\">Senha do Wi-Fi</label>"
    "<input id=\"senha\" name=\"senha\" type=\"password\" maxlength=\"64\" "
    "autocomplete=\"current-password\" placeholder=\"Deixe vazio para rede aberta\">"
    "<div class=\"hint\">A senha e armazenada somente no dispositivo.</div></div>"
    "<div class=\"divider\"></div><p class=\"section\">Servidor TecBot</p>"
    "<div class=\"field\"><label for=\"url\">Endereco do servidor (opcional)</label>"
    "<input id=\"url\" name=\"url\" type=\"url\" "
    "placeholder=\"http://192.168.1.10:3000\" maxlength=\"200\">"
    "<div class=\"hint\">Use o endereco do computador que executa o servidor.</div></div>"
    "<div class=\"field\"><label for=\"token\">Token do dispositivo (opcional)</label>"
    "<input id=\"token\" name=\"token\" type=\"password\" maxlength=\"256\" "
    "autocomplete=\"off\" placeholder=\"Token de acesso\"></div>"
    "<div class=\"test-grid\"><button class=\"test-button\" type=\"button\" id=\"test-mic\">"
    "Testar microfone</button><button class=\"test-button\" type=\"button\" id=\"test-sound\">"
    "Testar som</button><p class=\"test-status\" id=\"test-status\" role=\"status\" "
    "aria-live=\"polite\">Diga algo durante o teste do microfone ou toque o teste de som.</p></div>"
    "<div class=\"audio-settings\" aria-label=\"Controles de audio do ESP32\">"
    "<label class=\"audio-toggle\" for=\"mic-enabled\"><span>Microfone "
    "<small>(entrada de voz)</small></span><input id=\"mic-enabled\" type=\"checkbox\" checked></label>"
    "<label class=\"audio-toggle\" for=\"sound-enabled\"><span>Som "
    "<small>(alto-falante)</small></span><input id=\"sound-enabled\" type=\"checkbox\" checked></label>"
    "<label class=\"audio-toggle\" for=\"sound-volume\"><span>Volume do alto-falante</span>"
    "<output id=\"sound-volume-value\" for=\"sound-volume\">70%</output></label>"
    "<input id=\"sound-volume\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"70\">"
    "<p class=\"test-status\" id=\"audio-settings-status\" role=\"status\" "
    "aria-live=\"polite\">Carregando controles de áudio...</p></div>"
    "<button class=\"submit\" type=\"submit\">Salvar e conectar</button>"
    "</form><p class=\"foot\">Conecte-se ao ponto de acesso TecBot-Config</p></main>"
    "<script>"
    "const select=document.getElementById('ssid'),statusEl=document.getElementById('scan-status');"
    "const manualWrap=document.getElementById('manual-wrap'),manualInput=document.getElementById('manual-ssid');"
    "function atualizarManual(){const manual=select.value==='__manual__';"
    "manualWrap.style.display=manual?'block':'none';manualInput.required=manual;"
    "select.name=manual?'ssid-selecionado':'ssid';manualInput.name=manual?'ssid':''}"
    "async function buscarRedes(){const anterior=select.value;select.disabled=true;statusEl.className='status';"
    "statusEl.textContent='Buscando redes proximas...';"
    "try{const resposta=await fetch('/redes',{cache:'no-store'});"
    "const dados=await resposta.json();if(!resposta.ok)throw new Error(dados.mensagem||'Falha ao buscar redes.');"
    "select.replaceChildren(new Option('Selecione uma rede Wi-Fi',''));"
    "dados.redes.forEach(rede=>{const opcao=new Option(rede.ssid+(rede.segura?'  -  protegida':'')+'  -  '+rede.rssi+' dBm',rede.ssid);"
    "select.add(opcao)});select.add(new Option('Outra rede (digitar SSID)','__manual__'));"
    "if(anterior&&Array.from(select.options).some(opcao=>opcao.value===anterior)){select.value=anterior}"
    "else if(dados.atual&&Array.from(select.options).some(opcao=>opcao.value===dados.atual)){select.value=dados.atual}"
    "select.disabled=false;statusEl.className='status ok';"
    "statusEl.textContent=dados.redes.length?dados.redes.length+' rede(s) encontrada(s).':'Nenhuma rede encontrada; digite o SSID.';"
    "}catch(erro){select.replaceChildren(new Option('Selecione uma rede Wi-Fi',''),"
    "new Option('Outra rede (digitar SSID)','__manual__'));select.disabled=false;"
    "select.value='__manual__';statusEl.className='status error';statusEl.textContent=erro.message}"
    "atualizarManual()}"
    "select.addEventListener('change',atualizarManual);"
    "document.getElementById('refresh').addEventListener('click',buscarRedes);"
    "document.getElementById('config-form').addEventListener('submit',e=>{"
    "if(select.value==='__manual__'&&!manualInput.value.trim()){e.preventDefault();manualInput.focus()}});"
    "const testStatus=document.getElementById('test-status');"
    "async function executarTeste(rota,mensagem){const botoes=document.querySelectorAll('.test-button');"
    "botoes.forEach(item=>item.disabled=true);testStatus.className='test-status';"
    "testStatus.textContent=mensagem;try{const resposta=await fetch(rota,{method:'POST'});"
    "const dados=await resposta.json();if(!resposta.ok)throw new Error(dados.mensagem||'Falha no teste.');"
    "testStatus.className='test-status '+(dados.ok?'ok':'error');"
    "testStatus.textContent=dados.mensagem+(dados.rms!==undefined?' RMS: '+dados.rms+'; pico: '+dados.pico+'; limiar: '+dados.limiar+'.':'');"
    "}catch(erro){testStatus.className='test-status error';testStatus.textContent=erro.message}"
    "finally{botoes.forEach(item=>item.disabled=false)}}"
    "document.getElementById('test-mic').addEventListener('click',()=>"
    "executarTeste('/teste-microfone','Fale perto do microfone por 1 segundo...'));"
    "document.getElementById('test-sound').addEventListener('click',()=>"
    "executarTeste('/teste-som','Tocando um tom curto...'));"
    "const micEnabled=document.getElementById('mic-enabled');"
    "const soundEnabled=document.getElementById('sound-enabled');"
    "const soundVolume=document.getElementById('sound-volume');"
    "const soundVolumeValue=document.getElementById('sound-volume-value');"
    "const audioStatus=document.getElementById('audio-settings-status');"
    "async function carregarControlesAudio(){try{const resposta=await fetch('/controle/config',{cache:'no-store'});"
    "const dados=await resposta.json();if(!resposta.ok)throw new Error(dados.mensagem||'Falha ao carregar controles.');"
    "micEnabled.checked=dados.microfoneAtivo!==false;soundEnabled.checked=dados.somAtivo!==false;"
    "soundVolume.value=String(dados.volumeSom??70);soundVolumeValue.value=soundVolume.value+'%';"
    "audioStatus.className='test-status ok';audioStatus.textContent='Microfone e som configurados.';"
    "}catch(erro){audioStatus.className='test-status error';audioStatus.textContent=erro.message}}"
    "async function salvarControleAudio(campo,valor){"
    "const anteriorMicrofone=micEnabled.checked,anteriorSom=soundEnabled.checked,"
    "anteriorVolume=soundVolume.value;micEnabled.disabled=true;soundEnabled.disabled=true;"
    "soundVolume.disabled=true;audioStatus.className='test-status';"
    "audioStatus.textContent='Salvando configuração...';try{const corpo=new URLSearchParams();"
    "corpo.set(campo,valor);const resposta=await fetch('/controle/audio',{"
    "method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:corpo});"
    "const dados=await resposta.json();if(!resposta.ok)throw new Error(dados.mensagem||'Falha ao salvar.');"
    "micEnabled.checked=dados.microfoneAtivo;soundEnabled.checked=dados.somAtivo;"
    "soundVolume.value=String(dados.volumeSom);soundVolumeValue.value=soundVolume.value+'%';"
    "audioStatus.className='test-status ok';audioStatus.textContent=dados.mensagem;"
    "}catch(erro){micEnabled.checked=anteriorMicrofone;soundEnabled.checked=anteriorSom;"
    "soundVolume.value=anteriorVolume;soundVolumeValue.value=anteriorVolume+'%';"
    "audioStatus.className='test-status error';audioStatus.textContent=erro.message}"
    "finally{micEnabled.disabled=false;soundEnabled.disabled=false;soundVolume.disabled=false}}"
    "micEnabled.addEventListener('change',()=>salvarControleAudio('microfone',micEnabled.checked?'1':'0'));"
    "soundEnabled.addEventListener('change',()=>salvarControleAudio('som',soundEnabled.checked?'1':'0'));"
    "soundVolume.addEventListener('input',()=>{soundVolumeValue.value=soundVolume.value+'%'});"
    "soundVolume.addEventListener('change',()=>salvarControleAudio('volume',soundVolume.value));"
    "carregarControlesAudio();"
    "buscarRedes();</script></body></html>");
  return pagina;
}

void registrarRotasProvisionamento() {
  servidor.on("/teste-microfone", HTTP_POST, []() {
    if (!microfoneAtivo) {
      servidor.send(409, "application/json; charset=utf-8",
        "{\"ok\":false,\"mensagem\":\"Microfone desativado. Ative-o nas configuracoes para executar o teste.\"}");
      return;
    }
    if (!microfoneDisponivel) {
      servidor.send(503, "application/json; charset=utf-8",
        "{\"ok\":false,\"mensagem\":\"Microfone indisponivel. Aguarde a inicializacao e tente novamente.\"}");
      return;
    }
    if (capturandoAudio || audioTransacaoAtiva) {
      servidor.send(409, "application/json; charset=utf-8",
        "{\"ok\":false,\"mensagem\":\"O TecBot esta processando uma fala. Tente novamente em alguns segundos.\"}");
      return;
    }

    uint64_t somaQuadrados = 0;
    uint32_t pico = 0;
    size_t totalAmostras = 0;
    size_t errosLeitura = 0;
    unsigned long inicioTeste = millis();
    while (millis() - inicioTeste < 1000) {
      size_t bytesLidos = 0;
      esp_err_t resultado = i2s_read(
        PORTA_I2S_MICROFONE, bufferAmostrasMicrofone,
        sizeof(bufferAmostrasMicrofone), &bytesLidos,
        pdMS_TO_TICKS(100));
      if (resultado != ESP_OK ||
          bytesLidos > sizeof(bufferAmostrasMicrofone)) {
        errosLeitura++;
        continue;
      }

      size_t quantidade = bytesLidos / sizeof(int32_t);
      for (size_t i = 0; i < quantidade; i++) {
        int32_t amostra = constrain(
          bufferAmostrasMicrofone[i] >> 16, -32768, 32767);
        uint32_t magnitude = static_cast<uint32_t>(
          amostra < 0 ? -amostra : amostra);
        somaQuadrados += static_cast<int64_t>(amostra) * amostra;
        if (magnitude > pico) pico = magnitude;
      }
      totalAmostras += quantidade;
    }

    if (totalAmostras == 0) {
      Serial.printf("Teste do microfone sem amostras; erros I2S=%u.\n",
        static_cast<unsigned>(errosLeitura));
      servidor.send(500, "application/json; charset=utf-8",
        "{\"ok\":false,\"mensagem\":\"O microfone nao forneceu amostras. Verifique a ligacao I2S.\"}");
      return;
    }

    uint32_t rms = sqrt(
      static_cast<double>(somaQuadrados) / totalAmostras);
    DynamicJsonDocument documento(256);
    documento["ok"] = rms >= LIMIAR_AUDIO_RMS;
    documento["rms"] = rms;
    documento["pico"] = pico;
    documento["limiar"] = LIMIAR_AUDIO_RMS;
    documento["mensagem"] = rms >= LIMIAR_AUDIO_RMS
      ? "Microfone detectou sua voz."
      : "Nivel baixo. Fale mais perto e confira os fios do microfone.";
    String resposta;
    serializeJson(documento, resposta);
    Serial.printf("Teste do microfone: RMS=%lu, pico=%lu, amostras=%u.\n",
      static_cast<unsigned long>(rms), static_cast<unsigned long>(pico),
      static_cast<unsigned>(totalAmostras));
    servidor.send(200, "application/json; charset=utf-8", resposta);
  });

  servidor.on("/teste-som", HTTP_POST, []() {
    if (!somAtivo) {
      servidor.send(409, "application/json; charset=utf-8",
        "{\"ok\":false,\"mensagem\":\"Som desativado. Ative-o nas configuracoes para executar o teste.\"}");
      return;
    }
    if (capturandoAudio || audioTransacaoAtiva) {
      servidor.send(409, "application/json; charset=utf-8",
        "{\"ok\":false,\"mensagem\":\"O TecBot esta processando uma fala. Tente novamente em alguns segundos.\"}");
      return;
    }

    iniciarAmplificador();
    if (!amplificadorDisponivel) {
      servidor.send(503, "application/json; charset=utf-8",
        "{\"ok\":false,\"mensagem\":\"Amplificador indisponivel. Verifique os fios e reinicie o ESP32.\"}");
      return;
    }

    const uint32_t TAXA_TOM = 22050;
    const uint32_t FREQUENCIA_TOM = 660;
    const size_t QUADROS_POR_BLOCO = 128;
    const size_t TOTAL_QUADROS = TAXA_TOM / 3;
    int16_t amostras[QUADROS_POR_BLOCO * 2];
    esp_err_t resultado = i2s_set_clk(
      PORTA_I2S_AMPLIFICADOR, TAXA_TOM,
      I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    if (resultado != ESP_OK) {
      Serial.printf("Teste de som: falha ao ajustar clock I2S: %d\n",
        resultado);
      servidor.send(500, "application/json; charset=utf-8",
        "{\"ok\":false,\"mensagem\":\"Nao foi possivel configurar o audio I2S.\"}");
      return;
    }

    size_t quadroAtual = 0;
    while (quadroAtual < TOTAL_QUADROS) {
      size_t quantidade = TOTAL_QUADROS - quadroAtual;
      if (quantidade > QUADROS_POR_BLOCO) quantidade = QUADROS_POR_BLOCO;
      for (size_t i = 0; i < quantidade; i++) {
        size_t indice = quadroAtual + i;
        double envelope = 1.0;
        const size_t RAMPA = TAXA_TOM / 100;
        if (indice < RAMPA) {
          envelope = static_cast<double>(indice) / RAMPA;
        } else if (TOTAL_QUADROS - indice < RAMPA) {
          envelope = static_cast<double>(TOTAL_QUADROS - indice) / RAMPA;
        }
        double fase = 2.0 * PI * FREQUENCIA_TOM * indice / TAXA_TOM;
        int16_t valor = static_cast<int16_t>(
          sin(fase) * 6000 * envelope * volumeSom / 100);
        amostras[i * 2] = valor;
        amostras[i * 2 + 1] = valor;
      }

      size_t bytesEscritos = 0;
      size_t bytesSolicitados = quantidade * 2 * sizeof(int16_t);
      resultado = i2s_write(
        PORTA_I2S_AMPLIFICADOR, amostras, bytesSolicitados,
        &bytesEscritos, pdMS_TO_TICKS(1000));
      if (resultado != ESP_OK || bytesEscritos != bytesSolicitados) {
        Serial.printf(
          "Teste de som: falha I2S=%d, enviados=%u de %u bytes.\n",
          resultado, static_cast<unsigned>(bytesEscritos),
          static_cast<unsigned>(bytesSolicitados));
        servidor.send(500, "application/json; charset=utf-8",
          "{\"ok\":false,\"mensagem\":\"Falha ao enviar o tom ao amplificador I2S.\"}");
        return;
      }
      quadroAtual += quantidade;
    }

    capturaMicrofoneBloqueadaAte = millis() + 500;
    pausaMicrofonePosAudioAtiva = true;
    Serial.println("Teste de som: tom de 660 Hz reproduzido.");
    servidor.send(200, "application/json; charset=utf-8",
      "{\"ok\":true,\"mensagem\":\"Tom de teste enviado ao alto-falante.\"}");
  });

  servidor.on("/redes", HTTP_GET, []() {
    int totalRedes = WiFi.scanNetworks(false, true);
    if (totalRedes < 0) {
      WiFi.scanDelete();
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Nao foi possivel procurar redes Wi-Fi. Tente novamente.\"}");
      return;
    }

    DynamicJsonDocument documento(3072);
    documento["atual"] = ssidConfigurado;
    JsonArray redes = documento.createNestedArray("redes");
    for (int i = 0; i < totalRedes; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;

      bool duplicada = false;
      for (JsonObject rede : redes) {
        if (rede["ssid"].as<String>() == ssid) {
          duplicada = true;
          break;
        }
      }
      if (duplicada) continue;

      JsonObject rede = redes.createNestedObject();
      rede["ssid"] = ssid;
      rede["rssi"] = WiFi.RSSI(i);
      rede["segura"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();

    String resposta;
    serializeJson(documento, resposta);
    servidor.send(200, "application/json; charset=utf-8", resposta);
  });

  servidor.on("/", HTTP_GET, []() {
    servidor.send(200, "text/html; charset=utf-8", paginaProvisionamento());
  });

  servidor.on("/provisionar", HTTP_POST, []() {
    String ssid = servidor.hasArg("ssid") ? servidor.arg("ssid")
      : servidor.arg("ssid-selecionado");
    String senha = servidor.arg("senha");
    String url = servidor.arg("url");
    String token = servidor.arg("token");
    ssid.trim();
    url.trim();
    while (url.endsWith("/")) {
      url.remove(url.length() - 1);
    }

    if (!textoValido(ssid, 32)) {
      servidor.send(400, "text/html; charset=utf-8",
        "<h1>SSID invalido</h1><p>Use de 1 a 32 caracteres.</p>");
      return;
    }
    if (senha.length() > 64 ||
        (senha.length() > 0 && !textoValido(senha, 64))) {
      servidor.send(400, "text/html; charset=utf-8",
        "<h1>Senha invalida</h1><p>Use ate 64 caracteres.</p>");
      return;
    }
    if (url.length() > 0 && !validarUrlServidor(url)) {
      servidor.send(400, "text/html; charset=utf-8",
        "<h1>URL invalida</h1><p>Use http:// e um endereco da rede local.</p>");
      return;
    }
    if (token.length() > 0 && !tokenValido(token)) {
      servidor.send(400, "text/html; charset=utf-8",
        "<h1>Token invalido</h1><p>Use de 1 a 256 caracteres imprimiveis.</p>");
      return;
    }

    preferencias.putString("ssid", ssid);
    if (senha.length() > 0) {
      preferencias.putString("senha", senha);
    } else {
      preferencias.remove("senha");
    }
    if (url.length() > 0) {
      preferencias.putString("urlServidor", url);
    } else {
      preferencias.remove("urlServidor");
    }
    if (token.length() > 0) {
      preferencias.putString("deviceToken", token);
    } else {
      preferencias.remove("deviceToken");
    }

    servidor.send(200, "text/html; charset=utf-8",
      "<h1>Salvo!</h1><p>O TecBot vai reiniciar e conectar no Wi-Fi informado.</p>");
    reinicioPendente = true;
    horarioReinicio = millis() + 2000;
  });
}

void iniciarModoProvisionamento() {
  ultimaTentativaProvisionamento = millis();
  if (!WiFi.mode(WIFI_AP_STA)) {
    modoProvisionamentoAtivo = false;
    Serial.println("Falha ao ativar o modo Wi-Fi para o AP.");
    definirEstadoServidorOLED("falha no AP");
    return;
  }
  if (!WiFi.softAP(
      AP_PROVISIONAMENTO_SSID, AP_PROVISIONAMENTO_SENHA)) {
    modoProvisionamentoAtivo = false;
    Serial.println("Falha ao iniciar o ponto de acesso TecBot-Config.");
    definirEstadoServidorOLED("falha no AP");
    mostrarMensagemOLED("Falha ao abrir Wi-Fi de configuracao");
    return;
  }
  modoProvisionamentoAtivo = true;
  Serial.print("AP de provisionamento aberto: ");
  Serial.println(AP_PROVISIONAMENTO_SSID);
  Serial.println("Abra http://192.168.4.1 no navegador do celular.");
  Serial.print("IP do ponto de acesso: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("Clientes conectados ao AP: ");
  Serial.println(WiFi.softAPgetStationNum());
  definirEstadoServidorOLED("modo AP");
  mostrarMensagemOLED("Wi-Fi: TecBot-Config");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  inicioAbertura = millis();

  pinMode(PINO_HCSR04_TRIG, OUTPUT);
  pinMode(PINO_HCSR04_ECHO, INPUT);
  digitalWrite(PINO_HCSR04_TRIG, LOW);

  Wire.begin(PINO_OLED_SDA, PINO_OLED_SCL);
  telaDisponivel = tela.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!telaDisponivel) {
    Serial.println("Tela OLED SSD1306 nao encontrada no endereco 0x3C.");
  }

  if (telaDisponivel) {
    mostrarMensagemOLED("Inicializando...");
  }

  preferencias.begin("tecbot", false);
  urlServidor = preferencias.getString("urlServidor", "");
  ESP32_DEVICE_TOKEN = preferencias.getString("deviceToken", "");
  microfoneAtivo = preferencias.getBool("microfoneAtivo", true);
  somAtivo = preferencias.getBool("somAtivo", true);
  volumeSom = preferencias.getUChar("volumeSom", 70);
  if (volumeSom > 100) volumeSom = 70;
  modoHorarioAtivo = preferencias.getBool("modoHorario", false);
  bool telaEstavaDesativada = !preferencias.getBool("telaOLEDAtiva", true);
  telaOLEDAtiva = true;
  contrasteOLED = preferencias.getUChar("contrasteOLED", 180);
  if (contrasteOLED < 16) contrasteOLED = 180;
  textoInicialOLED = preferencias.getString("textoOLED", "TecBot pronto!");
  if (telaEstavaDesativada) {
    preferencias.putBool("telaOLEDAtiva", true);
    Serial.println("OLED reativada automaticamente na inicializacao.");
  }
  if (contrasteOLED == 180 &&
      preferencias.getUChar("contrasteOLED", 180) < 16) {
    preferencias.putUChar("contrasteOLED", contrasteOLED);
  }
  if (telaDisponivel) {
    tela.ssd1306_command(SSD1306_SETCONTRAST);
    tela.ssd1306_command(contrasteOLED);
    tela.ssd1306_command(telaOLEDAtiva
      ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  }
  if (urlServidor.length() > 0 && !validarUrlServidor(urlServidor)) {
    urlServidor = "";
    preferencias.remove("urlServidor");
    Serial.println("A URL salva do servidor e invalida; configure novamente.");
  }
  definirEstadoServidorOLED(
    urlServidor.length() > 0 ? "aguardando" : "sem URL");

  registrarRotasProvisionamento();

  servidor.on("/controle/config", HTTP_GET, []() {
    DynamicJsonDocument documento(384);
    documento["ssid"] = ssidConfigurado;
    documento["url"] = urlServidor;
    documento["tokenConfigurado"] = ESP32_DEVICE_TOKEN.length() > 0;
    documento["microfoneAtivo"] = microfoneAtivo;
    documento["somAtivo"] = somAtivo;
    documento["volumeSom"] = volumeSom;
    String resposta;
    serializeJson(documento, resposta);
    servidor.send(200, "application/json; charset=utf-8", resposta);
  });

  servidor.on("/controle/audio", HTTP_POST, []() {
    bool temMicrofone = servidor.hasArg("microfone");
    bool temSom = servidor.hasArg("som");
    bool temVolume = servidor.hasArg("volume");
    String valorMicrofone = servidor.arg("microfone");
    String valorSom = servidor.arg("som");
    String valorVolume = servidor.arg("volume");
    if ((!temMicrofone && !temSom && !temVolume) ||
        (temMicrofone && valorMicrofone != "0" && valorMicrofone != "1") ||
        (temSom && valorSom != "0" && valorSom != "1") ||
        (temVolume && (valorVolume.length() == 0 ||
          valorVolume.length() > 3 || valorVolume.toInt() < 0 ||
          valorVolume.toInt() > 100 ||
          String(valorVolume.toInt()) != valorVolume))) {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"Informe microfone e/ou som como 0 ou 1 e volume entre 0 e 100.\"}");
      return;
    }

    bool novoMicrofoneAtivo = temMicrofone
      ? valorMicrofone == "1" : microfoneAtivo;
    bool novoSomAtivo = temSom ? valorSom == "1" : somAtivo;
    uint8_t novoVolumeSom = temVolume
      ? static_cast<uint8_t>(valorVolume.toInt()) : volumeSom;
    if (temMicrofone &&
        preferencias.putBool("microfoneAtivo", novoMicrofoneAtivo) == 0) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar o estado do microfone.\"}");
      return;
    }
    if (temSom &&
        preferencias.putBool("somAtivo", novoSomAtivo) == 0) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar o estado do som.\"}");
      return;
    }
    if (temVolume &&
        preferencias.putUChar("volumeSom", novoVolumeSom) == 0) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar o volume do som.\"}");
      return;
    }

    if (temMicrofone && novoMicrofoneAtivo != microfoneAtivo) {
      microfoneAtivo = novoMicrofoneAtivo;
      if (!microfoneAtivo) {
        microfoneInicializacaoPendente = false;
        capturandoAudio = false;
        audioTransacaoPendente = false;
        quantidadeAmostrasAudio = 0;
        blocosAudio = 0;
        amostrasBlocoAudio = 0;
        estadoAudio = AUDIO_MIC_DESATIVADO;
      } else if (microfoneDisponivel) {
        estadoAudio = AUDIO_AGUARDANDO;
      } else {
        microfoneInicializacaoPendente = true;
        Serial.println("Ativacao do microfone solicitada; inicializacao agendada.");
      }
      estadoAudioExibido = AUDIO_ERRO_AUDIO;
    }
    if (temSom) somAtivo = novoSomAtivo;
    if (temVolume) volumeSom = novoVolumeSom;
    desenharOLED();

    DynamicJsonDocument documento(256);
    documento["ok"] = true;
    documento["microfoneAtivo"] = microfoneAtivo;
    documento["somAtivo"] = somAtivo;
    documento["volumeSom"] = volumeSom;
    documento["mensagem"] = String("Microfone ") +
      (microfoneAtivo ? "ativado" : "desativado") + "; som " +
      (somAtivo ? "ativado." : "desativado.") +
      " Volume " + String(volumeSom) + "%.";
    String resposta;
    serializeJson(documento, resposta);
    servidor.send(200, "application/json; charset=utf-8", resposta);
  });

  servidor.on("/controle/config", HTTP_POST, []() {
    String novoSsid = servidor.arg("ssid");
    String novaSenha = servidor.arg("senha");
    String novaUrl = servidor.arg("url");
    String novoToken = servidor.arg("token");
    novoSsid.trim();
    novaUrl.trim();
    while (novaUrl.endsWith("/")) {
      novaUrl.remove(novaUrl.length() - 1);
    }

    bool temSsid = servidor.hasArg("ssid") && novoSsid.length() > 0;
    bool temSenha = servidor.hasArg("senha");
    bool temUrl = servidor.hasArg("url") && novaUrl.length() > 0;
    bool temToken = servidor.hasArg("token") && novoToken.length() > 0;

    if (!temSsid && !temSenha && !temUrl && !temToken) {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"Envie ao menos um campo: ssid, senha, url ou token.\"}");
      return;
    }
    if (temSsid && !textoValido(novoSsid, 32)) {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"SSID inválido: use de 1 a 32 caracteres sem caracteres de controle.\"}");
      return;
    }
    if (temSenha && (novaSenha.length() > 64 ||
        (novaSenha.length() > 0 && !textoValido(novaSenha, 64)))) {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"Senha inválida: até 64 caracteres sem caracteres de controle (vazio = rede aberta).\"}");
      return;
    }
    if (temUrl && !validarUrlServidor(novaUrl)) {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"URL inválida. Use http:// e um endereço acessível na rede local; localhost não é permitido.\"}");
      return;
    }
    if (temToken && !tokenValido(novoToken)) {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"Token inválido: de 1 a 256 caracteres imprimíveis.\"}");
      return;
    }

    if (temSsid && preferencias.putString("ssid", novoSsid) == 0) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar o SSID.\"}");
      return;
    }
    if (temSenha && (novaSenha.length() > 0
        ? preferencias.putString("senha", novaSenha) == 0
        : preferencias.remove("senha") == false)) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar a senha.\"}");
      return;
    }
    if (temUrl && preferencias.putString("urlServidor", novaUrl) == 0) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar a URL do servidor.\"}");
      return;
    }
    if (temToken && preferencias.putString("deviceToken", novoToken) == 0) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar o token do dispositivo.\"}");
      return;
    }

    if (temUrl) urlServidor = novaUrl;
    if (temToken) ESP32_DEVICE_TOKEN = novoToken;
    ultimaConsultaServidor = 0;
    ultimoHeartbeatServidor = 0;
    definirEstadoServidorOLED(
      WiFi.status() == WL_CONNECTED ? "aguardando" : "sem Wi-Fi");

    if (temSsid || temSenha) {
      String ssidAlvo = temSsid ? novoSsid : ssidConfigurado;
      String senhaAlvo = temSenha
        ? novaSenha
        : preferencias.getString("senha", "");
      iniciarConexao(ssidAlvo, senhaAlvo);
    }

    servidor.send(200, "application/json; charset=utf-8",
      "{\"mensagem\":\"Configurações salvas.\"}");
  });

  servidor.on("/controle/status", HTTP_GET, []() {
    DynamicJsonDocument documento(512);
    documento["telaAtiva"] = telaOLEDAtiva;
    documento["contraste"] = contrasteOLED;
    documento["textoInicial"] = textoInicialOLED;
    documento["modoHorario"] = modoHorarioAtivo;
    documento["falando"] = falaEmAndamento;
    documento["microfoneAtivo"] = microfoneAtivo;
    documento["somAtivo"] = somAtivo;
    String resposta;
    serializeJson(documento, resposta);
    servidor.send(200, "application/json; charset=utf-8", resposta);
  });

  servidor.on("/controle/modo-horario", HTTP_POST, []() {
    String ativo = servidor.arg("ativo");
    if (ativo != "0" && ativo != "1") {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"Informe ativo como 0 ou 1.\"}");
      return;
    }
    bool novoModo = ativo == "1";
    if (preferencias.putBool("modoHorario", novoModo) == 0) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar o modo horário.\"}");
      return;
    }
    modoHorarioAtivo = novoModo;
    desenharOLED();
    servidor.send(200, "application/json; charset=utf-8",
      novoModo
        ? "{\"mensagem\":\"Modo horário ativado.\"}"
        : "{\"mensagem\":\"Modo horário desativado.\"}");
  });

  servidor.on("/controle/tela", HTTP_POST, []() {
    String ativo = servidor.arg("ativo");
    String novoContraste = servidor.arg("contraste");
    String novoTexto = servidor.arg("texto");
    novoTexto.trim();
    if ((ativo != "0" && ativo != "1") ||
        novoContraste.length() == 0 || novoContraste.length() > 3 ||
        novoTexto.length() > 32) {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"Informe estado, contraste entre 0 e 255 e texto de até 32 caracteres.\"}");
      return;
    }
    for (size_t i = 0; i < novoContraste.length(); i++) {
      if (novoContraste[i] < '0' || novoContraste[i] > '9') {
        servidor.send(400, "application/json; charset=utf-8",
          "{\"mensagem\":\"Contraste inválido.\"}");
        return;
      }
    }
    int valorContraste = novoContraste.toInt();
    if (valorContraste < 0 || valorContraste > 255) {
      servidor.send(400, "application/json; charset=utf-8",
        "{\"mensagem\":\"Contraste deve estar entre 0 e 255.\"}");
      return;
    }
    for (size_t i = 0; i < novoTexto.length(); i++) {
      if ((unsigned char)novoTexto[i] < 32 ||
          (unsigned char)novoTexto[i] == 127) {
        servidor.send(400, "application/json; charset=utf-8",
          "{\"mensagem\":\"O texto inicial não pode conter caracteres de controle.\"}");
        return;
      }
    }

    bool novoEstadoTela = ativo == "1";
    if (preferencias.putBool("telaOLEDAtiva", novoEstadoTela) == 0 ||
        preferencias.putUChar("contrasteOLED",
          static_cast<uint8_t>(valorContraste)) == 0 ||
        preferencias.putString("textoOLED", novoTexto) == 0) {
      servidor.send(500, "application/json; charset=utf-8",
        "{\"mensagem\":\"Não foi possível salvar as configurações da tela.\"}");
      return;
    }
    telaOLEDAtiva = novoEstadoTela;
    contrasteOLED = static_cast<uint8_t>(valorContraste);
    textoInicialOLED = novoTexto;
    tela.ssd1306_command(SSD1306_SETCONTRAST);
    tela.ssd1306_command(contrasteOLED);
    tela.ssd1306_command(telaOLEDAtiva
      ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
    if (telaOLEDAtiva) desenharOLED();
    servidor.send(200, "application/json; charset=utf-8",
      "{\"mensagem\":\"Configurações da OLED salvas.\"}");
  });

  servidor.on("/controle/reiniciar", HTTP_POST, []() {
    servidor.send(202, "application/json; charset=utf-8",
      "{\"mensagem\":\"Reiniciando o ESP32.\"}");
    reinicioPendente = true;
    horarioReinicio = millis() + 500;
  });

  String ssidSalvo = preferencias.getString("ssid", "");
  String senhaSalva = preferencias.getString("senha", "");
  iniciarModoProvisionamento();
  servidor.begin();
  Serial.println("Servidor de controle iniciado.");
  microfoneInicializacaoPendente = true;

  if (ssidSalvo.length() > 0) {
    iniciarConexao(ssidSalvo, senhaSalva);
  } else {
    ssidConfigurado = "";
  }
}

void loop() {
  servidor.handleClient();
  if (!modoProvisionamentoAtivo &&
      millis() - ultimaTentativaProvisionamento >= 5000) {
    iniciarModoProvisionamento();
  }
  if (microfoneInicializacaoPendente && millis() >= 5000) {
    microfoneInicializacaoPendente = false;
    if (microfoneAtivo) {
      Serial.println("Inicializando microfone apos a OLED e a rede.");
      mostrarMensagemOLED("Iniciando microfone...");
      iniciarMicrofone();
      if (microfoneDisponivel) {
        definirEstadoAudio(AUDIO_AGUARDANDO);
      } else {
        Serial.println("Falha ao inicializar o microfone; mantendo-o indisponivel.");
      }
    } else {
      Serial.println("Microfone desativado nas configuracoes.");
      definirEstadoAudio(AUDIO_MIC_DESATIVADO);
    }
    estadoAudioExibido = AUDIO_ERRO_AUDIO;
  }
  atualizarTelaAudio();
  if (reinicioPendente &&
      static_cast<long>(millis() - horarioReinicio) >= 0) {
    ESP.restart();
  }

  if (tentandoConectar) {
    if (WiFi.status() == WL_CONNECTED) {
      preferencias.putString("ssid", ssidTentativa);
      preferencias.putString("senha", senhaTentativa);
      ssidConfigurado = ssidTentativa;

      tentandoConectar = false;

      Serial.println("Wi-Fi conectado!");
      Serial.println(WiFi.localIP());
      definirEstadoServidorOLED(
        urlServidor.length() > 0 ? "aguardando" : "sem URL");
    } else if (millis() - inicioConexao >= 25000) {
      WiFi.disconnect(false, false);

      tentandoConectar = false;
      Serial.println("Falha ao conectar ao Wi-Fi salvo; TecBot-Config continua ativo.");
      definirEstadoServidorOLED("modo AP");
      mostrarMensagemOLED("Wi-Fi: TecBot-Config");
    }
  }

  enviarHeartbeatServidor();
  if (!capturandoAudio && !audioTransacaoAtiva) {
    consultarMensagemServidor();
  }

  if (falaEmAndamento) {
    if (millis() - inicioResposta >= DURACAO_CARINHA_FALANDO_MS) {
      falaEmAndamento = false;
      desenharOLED();
    } else if (millis() - ultimaAnimacaoRosto >= INTERVALO_ANIMACAO_ROSTO_MS) {
      ultimaAnimacaoRosto = millis();
      animacaoBocaAberta = !animacaoBocaAberta;
      desenharOLED();
    }
  }

  bool wifiConectadoAgora = WiFi.status() == WL_CONNECTED;
  if (wifiConectadoAgora != wifiConectadoMostrado) {
    wifiConectadoMostrado = wifiConectadoAgora;
    desenharOLED();
  }

  if (aberturaAtiva &&
      millis() - inicioAbertura >= DURACAO_ABERTURA_MS) {
    aberturaAtiva = false;
    desenharOLED();
  }

  processarAudioMicrofone();
  if (audioTransacaoPendente) {
    audioTransacaoPendente = false;
    Serial.printf(
      "Processando audio fora da rotina de captura; stack watermark=%u.\n",
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    processarTransacaoAudio();
  }

  if (!audioTransacaoAtiva && WiFi.status() == WL_CONNECTED &&
      millis() - ultimaLeituraHCSR04 >= INTERVALO_LEITURA_HCSR04) {
    ultimaLeituraHCSR04 = millis();
    float distanciaCm = medirDistanciaHCSR04();
    if (distanciaCm > 0) {
      enviarLeituraHCSR04(distanciaCm);
    }
  }

  if (existeProximaPaginaOLED &&
      millis() - ultimaPaginaOLED >= INTERVALO_PAGINA_OLED) {
    ultimaPaginaOLED = millis();
    size_t deslocamentoAtual = deslocamentoMensagemOLED;
    for (int linha = 0; linha < LINHAS_MENSAGEM_OLED; linha++) {
      proximaLinhaOLED(deslocamentoAtual);
    }
    deslocamentoMensagemOLED =
      deslocamentoAtual >= mensagemOLED.length() ? 0 : deslocamentoAtual;
    desenharOLED();
  }
}
