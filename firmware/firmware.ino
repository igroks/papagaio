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

int estadoAnt = 0; // VARIÁVEL PARA CONTROLAR A TROCA DE MODO DE OPERAÇÃO
char currentBus = 'i'; // BARRAMENTO ATIVO: 'i' = InfraVermelho, 'r' = RF 433MHz

void storeIRCode(IRData *aIRReceivedData, int index);
void sendIRCode(storedIRDataStruct *aIRDataToSend);
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
        // Assume 38 KHz
        IrSender.sendRaw(aIRDataToSend->rawCode, aIRDataToSend->rawCodeLength, 38);

        Serial.print(F("Sent raw "));
        Serial.print(aIRDataToSend->rawCodeLength);
        Serial.println(F(" marks or spaces"));
    } else {

        /*
         * Use the write function, which does the switch for different protocols
         */
        IrSender.write(&aIRDataToSend->receivedIRData, DEFAULT_NUMBER_OF_REPEATS_TO_SEND);

        Serial.print(F("Sent: "));
        printIRResultShort(&Serial, &aIRDataToSend->receivedIRData, false);
    }
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
