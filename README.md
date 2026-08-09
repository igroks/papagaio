# Papagaio

Controle universal **infravermelho + RF 433MHz** para ESP32. Ele escuta um
comando, grava e repete depois — como o pássaro.

Foi construído para clonar o controle do motor do portão da garagem (RF de
código fixo), mas o mesmo firmware grava e reenvia comandos de infravermelho
de TV, ar-condicionado e qualquer outro aparelho com controle remoto.

## Como funciona

Dois barramentos independentes, um ativo por vez, controlados por comandos de
um caractere no monitor serial (115200 baud):

| Comando | O que faz |
|---|---|
| `i` | ativa o barramento **infravermelho** |
| `r` | ativa o barramento **RF 433MHz** |
| `#` + `0`–`9` | grava o próximo código recebido no slot indicado |
| `0`–`9` | reenvia o código guardado naquele slot |

São 10 slots por barramento. Eles vivem só na RAM — **um reset ou queda de
energia apaga tudo**.

## Hardware

Placa: ESP32 DevKit V1 (30 pinos). Todos os módulos em 3.3V.

| Módulo | Pino do módulo | ESP32 |
|---|---|---|
| Transmissor RF (FS1000A) | DATA | GPIO 21 |
| Receptor RF (superheterodino 433MHz) | DATA | GPIO 23 |
| Transmissor IR (módulo c/ LED, ex: KY-005) | S | GPIO 19 |
| Receptor IR (VS1838B) | OUT | GPIO 22 |

Os quatro pinos de sinal ficam de propósito na mesma fileira física do
DevKit (`3V3, D19, D21, D22, D23`), porque só essa fileira era alcançável no
protoboard onde o projeto foi montado. Como não há VIN nessa fileira, o GND
chega por um único fio-ponte vindo do pino GND do lado oposto, e todos os
módulos rodam em 3.3V.

Nenhum resistor é necessário — os quatro são módulos prontos, com o resistor
limitador já embutido na placa.

Solde ~17cm de fio reto no pad `ANT` de cada módulo RF (1/4 do comprimento de
onda em 433MHz). Sem antena, o alcance cai para poucos centímetros.

> **Atenção:** o ESP32 não tolera 5V nas GPIOs. O receptor RF precisa ser
> alimentado em 3V3, porque a saída DATA dele acompanha a tensão de
> alimentação.

## Uso

```bash
make setup     # primeira vez: instala core ESP32 + IRremote e rc-switch
make flash     # compila e grava
make monitor   # abre o console serial
```

`make` sem argumentos lista todos os alvos. Para gravar sem `sudo`, seu
usuário precisa estar no grupo `dialout`
(`sudo usermod -aG dialout $USER`, depois faça logout/login).

Para clonar o controle do portão: `r`, depois `#0`, aperte o botão do controle
perto do receptor, e teste com `0`.

> Não use `arduino-cli monitor`, `screen` ou `cat /dev/ttyACM0` nesta placa —
> o bridge CH343 aciona DTR/RTS ao abrir a porta e o ESP32 sobe em modo
> download em vez de rodar o firmware. Use `make monitor`, que zera essas
> linhas e reseta com um pulso explícito no EN.

## Limitação: código rolante

Gravar e reenviar só funciona com controles de **código fixo**. Motores de
portão mais novos com segurança HCS301/KeeLoq trocam o código a cada
acionamento — nesses, o código gravado não vai abrir nada. Teste apertando o
mesmo botão do controle algumas vezes com `#0` armado: se o valor mudar a cada
aperto, é rolante.

## Origem

Começou como fork do sketch **"Controle IR" (Q0962)** de
[BrincandoComIdeias](https://github.com/canalBrincandoComIdeias/Q0962), do qual
a camada de gravação e reenvio de infravermelho é derivada. Depois foram
acrescentados o suporte a RF 433MHz (via
[rc-switch](https://github.com/sui77/rc-switch)), a seleção de barramento pela
serial, o remapeamento de pinos e a toolchain (Makefile e console serial).

Licenciado sob MIT — veja [LICENSE](LICENSE).
