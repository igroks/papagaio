/*
     PAPAGAIO — Controle universal IR + RF 433MHz para ESP32.
     Ele escuta um comando, grava e repete depois.

     Um dos usos: clonar o controle de código fixo do motor do portão.

     Baseado no sketch "Controle IR" (Q0962) de BrincandoComIdeias,
     https://cursodearduino.net — licenciado sob MIT, veja LICENSE.
     A camada de RF 433MHz, o remapeamento de pinos e a seleção de
     barramento pela serial foram acrescentados depois.
*/

#include <Arduino.h>

//#if RAMEND <= 0x4FF || (defined(RAMSIZE) && RAMSIZE < 0x4FF)
//#define RAW_BUFFER_LENGTH  120
//#elif RAMEND <= 0xAFF || (defined(RAMSIZE) && RAMSIZE < 0xAFF) // 0xAFF for LEONARDO
//#define RAW_BUFFER_LENGTH  500 // 600 is too much here, because we have additional uint8_t rawCode[RAW_BUFFER_LENGTH];
//#else
#define RAW_BUFFER_LENGTH  750
//#endif

#define MARK_EXCESS_MICROS  20 // 20 is recommended for the cheap VS1838 modules

#define pinLED 27

#include "PinDefinitionsAndMore.h" // Define macros for input and output pin etc. (IR_RECEIVE_PIN / IR_SEND_PIN)

// Pinos remapeados por causa da fiação do protoboard do usuário (todos na
// mesma fileira disponível: 3V3, D19, D21, D22, D23). Sobrescreve os
// valores padrão de PinDefinitionsAndMore.h (D15/D4) antes de incluir o
// IRremote, que usa essas macros em tempo de compilação.
#undef IR_RECEIVE_PIN
#undef IR_SEND_PIN
#define IR_RECEIVE_PIN   22 // DATA do receptor IR (VS1838B)
#define IR_SEND_PIN      19 // S/DATA do transmissor IR (módulo c/ LED)

#include <IRremote.hpp>
#include <RCSwitch.h>

// Pinos do módulo RF 433MHz (FS1000A + receptor superheterodino)
// Também na mesma fileira, e seguros: não são pinos de strapping, não fazem
// parte do barramento da flash interna (CLK/SD0/SD1) e não são a serial de
// programação/monitor (RX0/TX0).
#define RF_RECEIVE_PIN   23 // DATA do receptor 433MHz
#define RF_TRANSMIT_PIN  21 // DATA do transmissor FS1000A

RCSwitch rfSwitch = RCSwitch();

// Storage for the recorded IR code
struct storedIRDataStruct {
    IRData receivedIRData;  // extensions for sendRaw
    uint8_t rawCode[RAW_BUFFER_LENGTH]; // The durations if raw
    uint16_t rawCodeLength; // The length of the code
} sStoredIRData[10];

// Storage for the recorded RF code (portão / controles fixos 433MHz)
struct storedRFDataStruct {
    unsigned long value;      // Código numérico recebido
    unsigned int bitLength;   // Quantidade de bits do código
    unsigned int protocol;    // Protocolo detectado pela rc-switch (1-27)
    unsigned int pulseLength; // Duração do pulso (timing), em microssegundos
} sStoredRFData[10];

#ifndef LED_BUILTIN
#define LED_BUILTIN 2 // A maioria das placas ESP32 DevKit tem o LED onboard no GPIO2
#endif
int STATUS_PIN = LED_BUILTIN;

int DELAY_BETWEEN_REPEAT = 50;
int DEFAULT_NUMBER_OF_REPEATS_TO_SEND = 1;

// Frequencia da portadora usada no reenvio de codigos RAW (protocolo nao
// reconhecido). A maioria dos aparelhos usa 38kHz, mas alguns fabricantes
// usam 36, 40 ou 56 — e como o receptor IR nao consegue medir a portadora,
// nao da pra descobrir isso na gravacao, so testando. Ciclado pelo comando 'f'.
const uint8_t FREQUENCIAS[] = {38, 36, 40, 56};
uint8_t idxFrequencia = 0;

// Quantas vezes a mensagem RAW e repetida a cada envio. Controles de ar
// costumam mandar a mensagem varias vezes enquanto a tecla esta apertada.
uint8_t repeticoesRaw = 1;

int estadoAnt = 0; // VARIÁVEL PARA CONTROLAR A TROCA DE MODO DE OPERAÇÃO
char currentBus = 'i'; // BARRAMENTO ATIVO: 'i' = InfraVermelho, 'r' = RF 433MHz

void storeIRCode(IRData *aIRReceivedData, int index);
void sendIRCode(storedIRDataStruct *aIRDataToSend);
void autoTesteIR(int index);
void storeRFCode(int index);
void sendRFCode(storedRFDataStruct *aRFDataToSend);

void setup() {
    pinMode(pinLED, OUTPUT);
    digitalWrite(pinLED, LOW);

    Serial.begin(115200);

    IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
    IrSender.begin(); // No ESP32, IR_SEND_PIN e o feedback LED ja sao fixos em tempo de compilacao (API do IRremote >= 4.3)

    rfSwitch.enableTransmit(RF_TRANSMIT_PIN);
    rfSwitch.enableReceive(RF_RECEIVE_PIN);

    pinMode(STATUS_PIN, OUTPUT);

    Serial.println(F("=== Papagaio - controle universal IR + RF ==="));
    Serial.println(F("Digite 'i' para usar o barramento InfraVermelho"));
    Serial.println(F("Digite 'r' para usar o barramento RF 433MHz (portão)"));
    Serial.println(F("Para gravar digite '#' seguido da posicao entre 0 e 9"));
    Serial.println(F("Para enviar digite o numero entre 0 e 9 de um codigo ja gravado"));
    Serial.println(F("Digite 'f' para trocar a frequencia da portadora IR (38/36/40/56 kHz)"));
    Serial.println(F("Digite 'p' para trocar quantas vezes o codigo RAW e repetido (1..4)"));
    Serial.println(F("Digite 't' seguido da posicao para o auto-teste (envia e tenta receber de volta)"));
}

void loop() {
    static int index = -1;
    static int estadoAtual = 0;

    //RECEBE COMANDO DO SERIAL
    if (Serial.available()) {
      char recebido = Serial.read();

      if (recebido == 'i' || recebido == 'r') {
        // TROCA DE BARRAMENTO ATIVO
        currentBus = recebido;
        Serial.println(currentBus == 'i' ? F("Barramento: InfraVermelho") : F("Barramento: RF 433MHz (portao)"));

      } else if (recebido == '#') {
        // MODO GRAVAÇÃO
        estadoAtual = 1;
        Serial.println(F("Modo Gravacao"));
        delay(50);
        recebido = Serial.read();
        index = atoi(&recebido); // CONVERTE DE CHAR PARA INTEIRO
        Serial.print(F("Aguardando Codigo #"));
        Serial.println(recebido);
        digitalWrite(pinLED, HIGH);

      } else if (recebido == 'f') {
        // TROCA A FREQUENCIA DA PORTADORA USADA NO ENVIO RAW
        idxFrequencia = (idxFrequencia + 1) % (sizeof(FREQUENCIAS) / sizeof(FREQUENCIAS[0]));
        Serial.print(F("Portadora IR (envio RAW): "));
        Serial.print(FREQUENCIAS[idxFrequencia]);
        Serial.println(F(" kHz"));

      } else if (recebido == 'p') {
        // TROCA QUANTAS VEZES A MENSAGEM RAW E REPETIDA
        repeticoesRaw = (repeticoesRaw % 4) + 1;
        Serial.print(F("Repeticoes do envio RAW: "));
        Serial.println(repeticoesRaw);

      } else if (recebido == 't') {
        // AUTO-TESTE: ENVIA E TENTA CAPTURAR O PROPRIO SINAL
        delay(50);
        recebido = Serial.read();
        estadoAtual = 0;
        autoTesteIR(atoi(&recebido));

      } else if ( recebido >= '0' && recebido <= '9'){
        // MODO ENVIO
        Serial.println(F("Modo Envio"));
        estadoAtual = 2;
        index = atoi(&recebido); // CONVERTE DE CHAR PARA INTEIRO

      } else {
        IrReceiver.stop();
        rfSwitch.disableReceive();
        Serial.println(F("Receptores desabilitados."));
        estadoAtual = 0;
      }
    }

    //INICIA GRAVAÇÃO DE COMANDO
    if (estadoAtual != estadoAnt && estadoAtual == 1) {
        if (currentBus == 'i') {
            IrReceiver.start();
        } else {
            rfSwitch.enableReceive(RF_RECEIVE_PIN);
        }
    }

    //ENVIA COMANDO
    if (estadoAtual == 2 && index >= 0) {
        Serial.println(F("Enviando..."));
        digitalWrite(STATUS_PIN, HIGH);

        if (currentBus == 'i') {
            IrReceiver.stop();
            sendIRCode(&sStoredIRData[index]);
        } else {
            rfSwitch.disableReceive();
            sendRFCode(&sStoredRFData[index]);
        }

        digitalWrite(STATUS_PIN, LOW);

        index = -1; // INDEX < 0 PARA NÃO REPETIR O ENVIO
    }

    //GRAVA COMANDO
    if (estadoAtual == 1 && currentBus == 'i' && IrReceiver.available()) {
        storeIRCode(IrReceiver.read(), index);
        IrReceiver.resume(); // resume receiver
        digitalWrite(pinLED, LOW);
    }

    if (estadoAtual == 1 && currentBus == 'r' && rfSwitch.available()) {
        storeRFCode(index);
        rfSwitch.resetAvailable();
        digitalWrite(pinLED, LOW);
    }

    estadoAnt = estadoAtual;
}

void storeIRCode(IRData *aIRReceivedData, int index) {
    if (aIRReceivedData->flags & IRDATA_FLAGS_IS_REPEAT) {
        Serial.println(F("Ignore repeat"));
        return;
    }
    if (aIRReceivedData->flags & IRDATA_FLAGS_IS_AUTO_REPEAT) {
        Serial.println(F("Ignore autorepeat"));
        return;
    }
    if (aIRReceivedData->flags & IRDATA_FLAGS_PARITY_FAILED) {
        Serial.println(F("Ignore parity error"));
        return;
    }
    /*
     * Copy decoded data
     */
    sStoredIRData[index].receivedIRData = *aIRReceivedData;

    if (sStoredIRData[index].receivedIRData.protocol == UNKNOWN) {
        Serial.print(F("Received unknown code and store "));
        Serial.print(IrReceiver.decodedIRData.rawDataPtr->rawlen - 1);
        Serial.println(F(" timing entries as raw "));
        IrReceiver.printIRResultRawFormatted(&Serial, true); // Output the results in RAW format
        sStoredIRData[index].rawCodeLength = IrReceiver.decodedIRData.rawDataPtr->rawlen - 1;
        /*
         * Store the current raw data in a dedicated array for later usage
         */
        IrReceiver.compensateAndStoreIRResultInArray(sStoredIRData[index].rawCode);
    } else {
        IrReceiver.printIRResultShort(&Serial);
        IrReceiver.printIRSendUsage(&Serial);
        sStoredIRData[index].receivedIRData.flags = 0; // clear flags -esp. repeat- for later sending
        Serial.println();
    }
}

void sendIRCode(storedIRDataStruct *aIRDataToSend) {
    if (aIRDataToSend->receivedIRData.protocol == UNKNOWN /* i.e. raw */) {
        if (aIRDataToSend->rawCodeLength == 0) {
            Serial.println(F("Nenhum codigo IR gravado nessa posicao."));
            return;
        }

        for (uint8_t i = 0; i < repeticoesRaw; i++) {
            if (i > 0) {
                delay(DELAY_BETWEEN_REPEAT);
            }
            IrSender.sendRaw(aIRDataToSend->rawCode, aIRDataToSend->rawCodeLength, FREQUENCIAS[idxFrequencia]);
        }

        Serial.print(F("Sent raw "));
        Serial.print(aIRDataToSend->rawCodeLength);
        Serial.print(F(" marks or spaces @ "));
        Serial.print(FREQUENCIAS[idxFrequencia]);
        Serial.print(F(" kHz x"));
        Serial.println(repeticoesRaw);
    } else {

        /*
         * Use the write function, which does the switch for different protocols
         */
        IrSender.write(&aIRDataToSend->receivedIRData, DEFAULT_NUMBER_OF_REPEATS_TO_SEND);

        Serial.print(F("Sent: "));
        printIRResultShort(&Serial, &aIRDataToSend->receivedIRData, false);
    }
}

/*
 * Auto-teste: envia o codigo gravado e tenta captura-lo de volta com o proprio
 * receptor da placa. Aponte o LED IR para o receptor (uns 5cm, sem encostar) e
 * digite 't' seguido da posicao.
 *
 * Se o eco sair parecido com a gravacao original, o firmware esta emitindo o
 * waveform certo e o problema com o aparelho e alcance ou frequencia da
 * portadora. Se nao vier eco nenhum, o problema esta na emissao.
 *
 * No ESP32 o envio usa o LEDC e a recepcao usa um timer separado, entao da
 * pra manter o receptor ligado durante o envio — o que nao vale em outras
 * plataformas, onde os dois disputam o mesmo timer.
 */
void autoTesteIR(int index) {
    if (index < 0 || index > 9) {
        Serial.println(F("Posicao invalida para o auto-teste."));
        return;
    }

    Serial.print(F("Auto-teste da posicao #"));
    Serial.println(index);

    uint8_t repeticoesAnteriores = repeticoesRaw;
    repeticoesRaw = 1; // um disparo so, para o eco sair limpo

    IrReceiver.start();
    sendIRCode(&sStoredIRData[index]);
    repeticoesRaw = repeticoesAnteriores;

    unsigned long inicio = millis();
    while (millis() - inicio < 500) {
        if (IrReceiver.available()) {
            Serial.println(F("--- Eco capturado pelo proprio receptor ---"));
            IrReceiver.read();
            IrReceiver.printIRResultRawFormatted(&Serial, true);
            IrReceiver.resume();
            IrReceiver.stop();
            return;
        }
    }

    IrReceiver.stop();
    Serial.println(F("Nenhum eco capturado — o receptor nao viu o proprio LED."));
    Serial.println(F("Aponte o LED IR direto para o receptor e repita o teste."));
}

void storeRFCode(int index) {
    /*
     * Guarda o código, o tamanho em bits, o protocolo e o timing detectados
     * pela rc-switch. Isso é o suficiente para reenviar um comando fixo
     * (não-rolante) capturado do controle original do motor do portão.
     */
    sStoredRFData[index].value       = rfSwitch.getReceivedValue();
    sStoredRFData[index].bitLength   = rfSwitch.getReceivedBitlength();
    sStoredRFData[index].protocol    = rfSwitch.getReceivedProtocol();
    sStoredRFData[index].pulseLength = rfSwitch.getReceivedDelay();

    if (sStoredRFData[index].value == 0) {
        Serial.println(F("Codigo RF nao reconhecido (0 bits). Aproxime o controle do receptor e tente novamente."));
        return;
    }

    Serial.print(F("Codigo RF gravado #"));
    Serial.println(index);
    Serial.print(F("  Valor: "));
    Serial.println(sStoredRFData[index].value);
    Serial.print(F("  Bits: "));
    Serial.println(sStoredRFData[index].bitLength);
    Serial.print(F("  Protocolo: "));
    Serial.println(sStoredRFData[index].protocol);
    Serial.print(F("  Pulse Length (us): "));
    Serial.println(sStoredRFData[index].pulseLength);
}

void sendRFCode(storedRFDataStruct *aRFDataToSend) {
    if (aRFDataToSend->value == 0) {
        Serial.println(F("Nenhum codigo RF gravado nessa posicao."));
        return;
    }

    rfSwitch.setProtocol(aRFDataToSend->protocol);
    rfSwitch.setPulseLength(aRFDataToSend->pulseLength);
    rfSwitch.send(aRFDataToSend->value, aRFDataToSend->bitLength);

    Serial.print(F("Enviado RF: "));
    Serial.print(aRFDataToSend->value);
    Serial.print(F(" ("));
    Serial.print(aRFDataToSend->bitLength);
    Serial.println(F(" bits)"));
}
