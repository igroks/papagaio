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
| `f` | cicla a portadora IR usada no reenvio RAW: 38 → 36 → 40 → 56 kHz |
| `p` | cicla quantas vezes um código RAW é repetido por envio: 1 → 4 |
| `t` + `0`–`9` | auto-teste: envia o slot e tenta capturá-lo de volta |

São 10 slots por barramento. Eles vivem só na RAM — **um reset ou queda de
energia apaga tudo**.

Os comandos `f` e `p` só afetam códigos **RAW**, isto é, aqueles cujo protocolo
o IRremote não reconheceu. Como o receptor IR entrega o sinal já demodulado,
não há como medir a portadora original na gravação: 38kHz é o palpite inicial e
serve para a grande maioria dos aparelhos. O `f` existe para os poucos casos em
que não serve.

O `t` manda o código e deixa o receptor da própria placa ligado durante o
envio, imprimindo o que conseguiu capturar. Aponte o LED transmissor para o
receptor, a uns 5cm. Se o eco sair parecido com a gravação original, a emissão
está correta e qualquer falha com o aparelho real é de alcance ou de mira.

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

## Limitação: alcance do infravermelho

O LED transmissor é acionado direto pelo GPIO, o que limita a corrente a uns
20mA — o teto de um pino do ESP32. Um controle de fábrica pulsa o LED com
100–500mA. Na prática o reenvio funciona, mas exige apontar o LED para a
janelinha do receptor do aparelho, e o alcance é curto.

Se isso incomodar, o conserto é um transistor: base do BC337 (ou 2N2222) no
GPIO 19 através de 1kΩ, emissor no GND, e o LED IR com ~10Ω entre a
alimentação e o coletor. O GPIO passa a chavear só a base, e o LED sai do
orçamento de corrente do pino. Alimentar o LED com 5V em vez de 3.3V dobra o
ganho de novo — o nível lógico continua seguro, porque a GPIO nunca vê os 5V.

Trocar o módulo por um LED nu **não** resolve: o gargalo é o pino, não o LED.

## Origem

Começou como fork do sketch **"Controle IR" (Q0962)** de
[BrincandoComIdeias](https://github.com/canalBrincandoComIdeias/Q0962), do qual
a camada de gravação e reenvio de infravermelho é derivada. Depois foram
acrescentados o suporte a RF 433MHz (via
[rc-switch](https://github.com/sui77/rc-switch)), a seleção de barramento pela
serial, o remapeamento de pinos e a toolchain (Makefile e console serial).

Licenciado sob MIT — veja [LICENSE](LICENSE).
