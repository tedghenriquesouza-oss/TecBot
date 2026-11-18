# TecBot

O TecBot é um assistente web com backend Python/Flask e interface responsiva baseada em Bootstrap. O navegador envia mensagens ao agente de IA; respostas geradas com sucesso também ficam disponíveis ao ESP32 por HTTP para exibição em uma tela OLED SSD1306. O chat e as configurações são compartilhados por todos que acessam o servidor, e o histórico recente é usado como contexto em novas respostas. Isso não retreina os pesos do modelo.

O HC-SR04 integrado ao ESP32 funciona como um **ambiente de testes**: leituras são enviadas ao backend, detecções são mostradas no dashboard e a IA pode analisá-las e sugerir experiências. Isso não treina nem altera os pesos do modelo e não executa ações físicas automaticamente.

## Componentes do projeto

- `app.py`: API Flask, integração com Gemini ou NVIDIA, persistência SQLite das conversas e rotas para o dispositivo.
- `piper_service.py`: integração do backend Flask com o serviço local Piper.
- `index.html`, `dashboard.html` e `settings.html`: páginas separadas para chat, dashboard e configurações.
- `src/chat.js`, `src/dashboard.js`, `src/settings.js` e `src/shared.js`: lógica de cada página, tema e integração com a API.
- `src/services/api.js` e `src/styles/`: chamadas ao backend e tema visual responsivo em tons neutros, com opção clara ou escura.
- `settings.json`: preferências não secretas do assistente, como provedor, modelo, instruções e voz.
- `tecbot.sqlite3`: banco criado automaticamente para conversas e mensagens.
- `CODPROJETOESP32/CODPROJETOESP32.ino`: firmware do ESP32, página de configuração Wi-Fi, configuração da URL do servidor, polling HTTP e exibição na OLED.
- `models/voices/` e `scripts/`: modelo de voz e recursos de instalação do Piper.

## Requisitos

- Python 3.9 ou mais recente, com as dependências de `requirements.txt`.
- Node.js e npm para gerar a interface de produção ou executar o Vite em desenvolvimento.
- Uma chave de API para o provedor escolhido (Gemini ou NVIDIA).
- Para compilar o firmware: Arduino IDE, core **esp32 by Espressif Systems** e as bibliotecas listadas na seção [Firmware ESP32](#firmware-esp32).

## Configuração do backend

Configure as variáveis de ambiente localmente, por exemplo em `.env`. Não coloque valores reais de credenciais neste documento, no frontend ou no firmware compartilhado:

| Variável | Uso |
| --- | --- |
| `GEMINI_API_KEY` | Chave do Google Gemini; também é usada para transcrever áudio do ESP32. |
| `NVIDIA_API_KEY` | Chave NVIDIA quando o provedor selecionado for NVIDIA. |
| `ESP32_DEVICE_TOKEN` | Token Bearer que protege as rotas autenticadas do dispositivo. Deve corresponder ao valor configurado localmente no firmware. |
| `PORT` | Porta HTTP do servidor; se omitida, o backend usa `3000`. |

O servidor escuta em `0.0.0.0`, permitindo acesso por outros dispositivos na rede local. Configure o firewall do computador para permitir conexões na porta escolhida, se necessário. Não exponha o servidor à internet pública: configurações, conversas e controles do dashboard estão sem autenticação e são acessíveis a qualquer pessoa que consiga alcançar o servidor.

As preferências do agente ficam em `settings.json`. Na página `/settings.html`, é possível substituir as chaves Gemini/NVIDIA e cadastrar provedores compatíveis com OpenAI (URL base, modelo e token). Esses segredos são gravados localmente em `.provider-config.json`, não são retornados pela API e têm precedência sobre as variáveis de ambiente correspondentes. O arquivo está excluído do Git. O cadastro de provedores personalizados aceita endpoints HTTP/HTTPS confiáveis; mantenha o servidor acessível apenas a pessoas de confiança.

## Executar a interface e o servidor

Instale as dependências Python e JavaScript e gere os arquivos da interface:

```powershell
python -m pip install -r requirements.txt
npm install
npm run build
```

Inicie o backend Flask:

```powershell
python app.py
```

Ou use `npm start` para executar o mesmo comando.

Abra `http://localhost:3000/` no próprio computador. Em um celular ou outro dispositivo conectado à mesma rede Wi-Fi, abra `http://IP_DO_COMPUTADOR:3000/`, substituindo pelo endereço local do computador que executa o servidor.

O chat, o painel e as preferências também estão disponíveis em `/`, `/dashboard.html` e `/settings.html`. Use o controle **Tema claro/escuro** no cabeçalho; a escolha fica salva neste navegador.

Durante o desenvolvimento da interface, use:

```powershell
npm run dev
```

O Vite informa o endereço da interface no terminal e encaminha as chamadas `/api` ao backend local na porta `3000`. Para testar a versão de produção servida pelo Flask, execute o build antes de iniciar o servidor.

## API principal

### Conversa

`POST /api/chat` recebe uma mensagem em JSON:

```json
{
  "mensagem": "Olá, TecBot!"
}
```

Quando a geração termina com sucesso, retorna o contrato usado pela interface:

```json
{
  "resposta": "Olá! Como posso ajudar?"
}
```

A conversa pode ser enviada pela interface web, por um cliente HTTP ou por comando de voz no ESP32. No ESP32, diga “TecBot” antes da pergunta, por exemplo: “TecBot, que horas são?”. O servidor transcreve o áudio para verificar a palavra de ativação; somente o texto posterior a “TecBot” é enviado ao agente e salvo como pergunta. Áudios sem a palavra de ativação (ou sem pergunta depois dela) são ignorados e não criam conversa nem resposta. Como a detecção é feita após a transcrição, o áudio é enviado ao servidor para essa verificação.

No chat web, comandos como “tocar música X” pesquisam o primeiro resultado no YouTube e mostram seu link. O ESP32 não reproduz áudio diretamente de vídeos do YouTube; ele só reproduz respostas de voz geradas pelo serviço Piper do servidor.

### Última resposta para o ESP32

`GET /api/device/message` exige o cabeçalho:

```text
Authorization: Bearer <ESP32_DEVICE_TOKEN>
```

Exemplo de resposta:

```json
{
  "id": 1,
  "texto": "Olá! Como posso ajudar?"
}
```

Antes da primeira resposta gerada com sucesso, o endpoint retorna `{"id":0,"texto":""}`. Uma nova geração bem-sucedida incrementa o ID; falhas da IA não substituem a mensagem anterior. O texto entregue ao dispositivo é limitado a 4.000 caracteres, sem alterar a resposta completa retornada ao navegador. A mensagem e o contador ficam somente na memória: são perdidos quando o processo do servidor reinicia.

Se `ESP32_DEVICE_TOKEN` não estiver configurado, o endpoint retorna HTTP `503`. Um token ausente ou incorreto na requisição retorna HTTP `401`.

### Síntese de fala

`POST /api/device/speech` exige o mesmo token Bearer e recebe o ID da mensagem mais recente:

```json
{
  "id": 1
}
```

Quando o ID corresponde à mensagem atual, o servidor gera e retorna `audio/wav` usando o Piper e a velocidade configurada. O firmware solicita esse áudio após exibir a transcrição e o reproduz pelo MAX98357A. IDs inválidos retornam HTTP `400`; se a mensagem já tiver mudado, retorna HTTP `409`.

### Leituras de teste do HC-SR04

`POST /api/devices/sensor-reading` recebe leituras autenticadas do ESP32:

```json
{
  "deviceId": "esp32-1",
  "distanceCm": 24.7
}
```

O dispositivo esperado é `ESP32_DEVICE_ID` ou, por padrão, `esp32-1`. Distâncias fora do intervalo de 2 a 400 cm são rejeitadas. A rota retorna HTTP `202` quando uma nova detecção é registrada (distância de até 30 cm); o estado só rearma quando a distância alcança 35 cm. Outras leituras válidas retornam HTTP `200` e atualizam o histórico usado pelo dashboard.

Uma nova detecção inicia uma análise em segundo plano com o provedor de IA configurado. A ingestão da leitura não aguarda a geração; o resultado, o estado e uma janela limitada de leituras aparecem em `GET /api/devices`, que alimenta o dashboard. Há um intervalo mínimo de 30 segundos entre análises por dispositivo. Eventos durante esse intervalo são armazenados, mas não chamam a IA. Os dados são mantidos em memória e se perdem ao reiniciar o servidor.

### Outras rotas

| Método | Rota | Finalidade |
| --- | --- | --- |
| `GET` | `/api/health` | Estado e informações de operação do backend. |
| `GET` | `/api/status` | Estado básico do servidor. |
| `GET` | `/api/device-config` | Configuração pública básica para o dispositivo; não retorna tokens. |
| `GET` | `/api/devices` | Lista de dispositivos e telemetria recebida. |
| `GET` | `/api/devices/task-logs` | Lista paginada de perguntas por voz e atividades registradas do ESP32. |
| `POST` | `/api/devices/heartbeat` | Atualiza o contato do dispositivo; exige token Bearer. |
| `POST` | `/api/devices/telemetry` | Envia telemetria; exige token Bearer. |
| `POST` | `/api/devices/sensor-reading` | Envia leitura HC-SR04; exige token Bearer e pode registrar uma detecção/análise. |
| `POST` | `/api/device/voice-command` | Recebe WAV do INMP441, transcreve com Gemini e envia o texto ao chat; exige token Bearer. |
| `GET` | `/api/models?provider=gemini` | Consulta o catálogo de modelos Gemini. Use `provider=nvidia` para NVIDIA. |
| `GET` | `/api/conversations` | Lista as conversas compartilhadas. |
| `GET` | `/api/conversations/{id}` | Lê as mensagens de uma conversa. |
| `POST` | `/api/conversations` | Cria uma conversa vazia. |
| `DELETE` | `/api/conversations/{id}` | Exclui uma conversa e suas mensagens. |
| `POST` | `/api/chat` | Envia uma mensagem, usa histórico compartilhado como contexto e salva a troca. |
| `GET` | `/api/settings` | Lê configurações compartilhadas. |
| `PUT` | `/api/settings` | Salva configurações compartilhadas. |
| `POST` | `/api/settings/reset` | Restaura as configurações padrão. |
| `GET` | `/api/provider-config` | Lista provedores e informa apenas se cada token está configurado; nunca retorna os segredos. |
| `PUT` | `/api/provider-config` | Atualiza tokens integrados e provedores compatíveis com OpenAI. |
| `GET` | `/api/voice/status` | Estado do serviço local de voz. |
| `POST` | `/api/voice/synthesize` | Gera áudio com o serviço local de voz. |

## Firmware ESP32

O código completo está em `CODPROJETOESP32/CODPROJETOESP32.ino`. Ele mantém o ponto de acesso e a página de configuração existentes, permite configurar a URL-base do servidor e reutiliza as rotas atuais de mensagem, transcrição e síntese de voz. Na página local `http://192.168.4.1/`, os botões **Testar microfone** medem o RMS durante um segundo (fale enquanto mede) e **Testar som** reproduz um tom curto de 660 Hz pelo MAX98357A. Os controles **Microfone (entrada de voz)** e **Som (alto-falante)** ativam ou desativam cada função separadamente, salvando a escolha no ESP32. A captura do microfone fica suspensa durante as transações de áudio e por 500 ms após a reprodução, reduzindo interferência sem impedir respostas faladas. O controle **Volume do alto-falante** ajusta de 0 a 100% tanto a reprodução de respostas quanto o tom de teste, e também é persistido. Os testes ficam disponíveis depois que o microfone termina de inicializar.

O dashboard (`/dashboard.html`) também permite ativar ou desativar remotamente o microfone e o alto-falante do ESP32 conectado. O estado atual é sincronizado pelo endpoint `/controle/status`; para isso, atualize o firmware com os campos de áudio incluídos no status.

O mesmo firmware lê o HC-SR04 aproximadamente a cada 3 segundos e envia a distância à rota `/api/devices/sensor-reading`. O `deviceId` padrão é `esp32-1`, correspondente ao backend quando `ESP32_DEVICE_ID` não está definido. Se configurar outro ID no servidor, atualize também `ID_DISPOSITIVO` no firmware.

Com o INMP441 conectado, o ESP32 monitora o áudio continuamente e só inicia uma gravação após três blocos de voz consecutivos; gravações com menos de 180 ms de voz são descartadas localmente. Os trechos válidos são fechados após detectar silêncio, comprimidos em IMA ADPCM e enviados ao endpoint autenticado `POST /api/device/voice-command`. A captura e a reprodução usam controladores I2S separados (I2S_NUM_0 e I2S_NUM_1, no ESP32 WROOM-32), e a captura fica suspensa até a resposta ser reproduzida. O servidor transcreve com a chave Gemini configurada em `/settings.html` ou `GEMINI_API_KEY`, mesmo quando outro provedor está selecionado para o chat. Só perguntas precedidas por “TecBot” são enviadas ao chat. Respostas e erros de transcrição ou processamento são sintetizados pelo Piper para reprodução no ESP32. O limiar de detecção `LIMIAR_AUDIO_RMS` pode precisar de ajuste conforme o microfone e o ruído do ambiente.

O endpoint recebe áudio WAV IMA ADPCM mono de 16 kHz e exige `Authorization: Bearer <ESP32_DEVICE_TOKEN>`. A transcrição é encaminhada ao mesmo fluxo de chat usado pelo dashboard; a resposta fica disponível para o firmware na consulta autenticada `/api/device/message`.

No fluxo de voz do ESP32, o backend salva a transcrição como mensagem do usuário e publica a resposta gerada pelo agente em `/api/device/message`. O ESP32 exibe essa resposta e a rota existente de síntese `/api/device/speech` gera o áudio que será reproduzido.

Para o ESP32 aparecer no dashboard mesmo sem o HC-SR04 conectado, o firmware envia `POST /api/devices/heartbeat` autenticado a cada 20 segundos. A lista do dashboard é mantida em memória pelo servidor, então o dispositivo passa a aparecer depois do primeiro heartbeat e sua condição de conectado expira após 90 segundos sem novos contatos.

A tela **Registros do ESP32**, em `/esp-logs.html`, mantém no SQLite as perguntas e respostas enviadas pelo microfone, reconexões, detecções de proximidade e comandos/configurações realizados pelo dashboard. Os registros são carregados em páginas de até 100 itens e permanecem disponíveis após reiniciar o servidor.

O dashboard oferece controles remotos da OLED, do modo horário, da configuração de rede e da reinicialização do ESP32. Esses controles não exigem token administrativo; use apenas em rede confiável. O servidor encaminha os comandos permitidos ao endereço IPv4 privado aprendido no heartbeat; o token do dispositivo permanece no firmware.

Para a página do dashboard alcançar o ESP32, o telefone/navegador, o computador servidor e o ESP32 devem estar na mesma rede local, e o backend precisa receber heartbeats autenticados do dispositivo. Se a rede separar clientes Wi-Fi (isolamento de clientes/AP), os comandos remotos não alcançam o ESP32; use a página local `http://192.168.4.1/` para a configuração inicial.

### Hardware OLED

Para o ESP32 WROOM-32 e uma OLED monocromática SSD1306 I²C de 128x64:

| OLED | ESP32 |
| --- | --- |
| GND | GND |
| VDD/VCC | 3V3 |
| SCK/SCL | GPIO 22 |
| SDA | GPIO 21 |

| HC-SR04 | ESP32 |
| --- | --- |
| VCC | 5V/VIN apropriado para o módulo |
| GND | GND comum |
| TRIG | GPIO 5 |
| ECHO | GPIO 18 através de divisor de tensão para no máximo 3,3 V |

**Proteja o GPIO 18:** a saída ECHO do HC-SR04 pode ser de 5 V e não deve ser ligada diretamente ao ESP32. Use um divisor de tensão ou conversor de nível lógico para reduzir ECHO a 3,3 V. Confirme a alimentação exigida pela versão do módulo.

### Microfone INMP441

| INMP441 | ESP32 WROOM-32 |
| --- | --- |
| VDD | 3V3 |
| GND | GND comum |
| SCK/BCLK | GPIO 14 |
| WS/LRCL | GPIO 15 |
| SD/DOUT | GPIO 32 |
| L/R | GND (canal esquerdo) |

Alimente o INMP441 somente com 3,3 V. O firmware captura I2S a 16 kHz, mono, e comprime em IMA ADPCM antes de armazenar o áudio, reduzindo substancialmente o uso de RAM do ESP32 WROOM-32. Se o ambiente estiver muito ruidoso ou o microfone não detectar fala, ajuste `LIMIAR_AUDIO_RMS` no firmware.

### Amplificador MAX98357A

| MAX98357A | ESP32 |
| --- | --- |
| LRC/WS | GPIO 19 |
| BCLK | GPIO 27 |
| DIN | GPIO 23 |
| GAIN | Sem conexão |
| SD | 3V3 |
| GND | GND comum |
| VIN | 5V |

O firmware inicializa primeiro a OLED e a rede; o microfone é iniciado depois e o controlador I2S do MAX98357A só é instalado quando uma resposta WAV válida está pronta para reprodução. O MCLK não é usado pelo INMP441 nem pelo MAX98357A e fica explicitamente desativado; ambos usam somente BCLK, WS/LRC e dados. O WAV da rota existente `/api/device/speech` aceita PCM mono/estéreo de 8, 16, 24 ou 32 bits entre 8 e 48 kHz, converte as amostras para saída I2S de 16 bits e mantém a taxa/canais indicados. A saída usa o segundo controlador I2S do ESP32 WROOM-32, separado do microfone. A OLED mostra um rosto em modo de escuta durante a captura e anima a boca durante a reprodução. A captura fica suspensa durante envio e reprodução.

O firmware inicializa o barramento nos GPIO 21/22 e tenta encontrar a tela no endereço I²C `0x3C`. Se a inicialização falhar, informa o problema no Serial Monitor e continua executando o restante do firmware. O texto é convertido para caracteres compatíveis com a fonte padrão, quebrado em linhas e paginado na tela.

### Bibliotecas Arduino

Instale pelo Library Manager da Arduino IDE:

- **ArduinoJson**
- **Adafruit GFX Library**
- **Adafruit SSD1306**
- **Adafruit BusIO**, dependência da biblioteca Adafruit SSD1306

`WiFi.h`, `WebServer.h`, `Preferences.h`, `Wire.h`, `HTTPClient.h` e `WiFiClient.h` são fornecidas pelo core ESP32/ambiente Arduino correspondente.

### Valores que precisam ser configurados

1. Configure `ESP32_DEVICE_TOKEN` no ambiente local do backend sem compartilhar esse valor.
2. Grave o firmware e conecte o computador ou celular ao ponto de acesso `TecBot-Config`, usando o exemplo de senha configurado no código, por exemplo `TroqueSuaSenha123`. Antes de compartilhar o firmware, altere essa senha no código para um valor próprio. O AP inicia em modo `WIFI_AP` antes da tentativa de conexão com a rede salva e permanece ativo em modo `WIFI_AP_STA` quando o ESP32 tenta conectar-se à rede local.
3. Abra `http://192.168.4.1/`, informe a URL do servidor, por exemplo `http://192.168.1.70:3000`, e salve-a. Use o IP local do computador que executa o backend; `localhost` não funciona para o ESP32. A URL é persistida em `Preferences`.
4. Na mesma página local, informe o token configurado no servidor e salve-o. O campo é do tipo senha; o firmware armazena o valor em `Preferences`, não o retorna em nenhuma rota e não o imprime no Serial Monitor. A configuração do token só é aceita quando o cliente está conectado ao ponto de acesso `TecBot-Config`.
5. Selecione a rede Wi-Fi e informe a senha. As credenciais de Wi-Fi são mantidas em `Preferences` para reconexão após reiniciar o ESP32.
6. Configure a chave do Google Gemini na página `/settings.html` (ou em `GEMINI_API_KEY`) para habilitar a transcrição de áudio do INMP441, inclusive se o provedor principal selecionado for NVIDIA ou outro provedor.
7. Se o servidor estiver usando uma porta diferente, informe essa porta na URL. O computador e o ESP32 precisam ter conectividade entre si na rede.

### Consulta HTTP

Após reconhecer a palavra de ativação e a pergunta, o servidor disponibiliza a resposta em `/api/device/message`; o firmware exibe a resposta e solicita o WAV pela rota existente `POST /api/device/speech`. Essa resposta é sintetizada com Piper no servidor; não há envio de texto como se fosse áudio.

### Rota de testes do HC-SR04

O ESP32 envia leituras válidas entre 2 e 400 cm para:

```text
POST /api/devices/sensor-reading
Authorization: Bearer <ESP32_DEVICE_TOKEN>
Content-Type: application/json
```

Corpo:

```json
{
  "deviceId": "esp32-1",
  "distanceCm": 24.7
}
```

O backend considera uma detecção ao entrar em até 30 cm e só rearma após a distância atingir pelo menos 35 cm. Essa histerese evita notificações repetidas enquanto o objeto permanece próximo. Leituras e um histórico curto são mantidos em memória e apresentados no dashboard, que atualiza a cada 5 segundos enquanto está aberto. Reiniciar o servidor Flask apaga o histórico.

Uma nova detecção produz um evento e solicita uma análise curta ao provedor de IA configurado. Análises por ESP32 têm intervalo mínimo de 30 segundos para limitar chamadas e custos; eventos dentro desse intervalo continuam registrados, mas sem nova análise. A rota retorna HTTP `202` para a borda de detecção e a análise aparece no dashboard quando termina. Falhas da IA não invalidam a leitura do sensor.

Este recurso é um **teste de leitura e interpretação**, não treinamento de modelo. A resposta da IA é apenas uma sugestão textual; o firmware não executa ações físicas a partir dela.

## Testes e verificações

- Verifique a sintaxe do backend: `python -m py_compile app.py piper_service.py`.
- Execute os testes de API e contexto: `npm test`.
- Gere a interface: `npm run build`.
- Inicie o servidor Flask: `python app.py` (ou `npm start`).
- Abra a interface em `http://localhost:3000/` ou no IP local do servidor.

As conversas ficam no arquivo `tecbot.sqlite3`. Todos os usuários do mesmo servidor compartilham o histórico; excluir uma conversa remove suas mensagens e deixa de incluí-las no contexto futuro. As conversas não treinam nem alteram os pesos do modelo de IA.
- Envie uma mensagem pela página e confirme a resposta na conversa.
- Consulte `/api/device/message` com o token Bearer configurado e verifique o ID e o texto mais recentes. Sem token válido, o servidor deve responder com HTTP `401`.
- No hardware, confirme a OLED, a reconexão Wi-Fi e o estado do servidor no display. Gere uma resposta nova pela interface para observar a atualização da tela.
- Conecte o HC-SR04 com o divisor no pino ECHO, abra o dashboard e aproxime um objeto até 30 cm. Confira a distância, a amostra no histórico, o evento e a sugestão de IA.

`npm test` executa os testes Python do backend. A compilação do firmware deve ser feita pela Arduino IDE ou por `arduino-cli` com o core ESP32 e as bibliotecas instaladas. A compilação atual para ESP32 usa cerca de 1,07 MB de flash; se a variante da placa ou as opções de build excederem a partição padrão, selecione **Tools > Partition Scheme > Huge APP (3MB No OTA/1MB SPIFFS)**, que não oferece atualização OTA.

## Segurança

- Nunca publique chaves de API, tokens, senhas Wi-Fi ou arquivos `.env`.
- Use valores locais e não compartilhe capturas da IDE/Serial Monitor que possam conter credenciais.
- O token no firmware é um segredo compilado dentro do binário; distribua o firmware somente a dispositivos confiáveis e troque o token no servidor e no dispositivo se ele for exposto.
- A URL do ESP32 deve apontar para um endereço alcançável na rede local; não use `localhost` nem exponha o backend publicamente apenas para conectar o dispositivo.
