#!/usr/bin/env python3
"""Console serial interativo para o ESP32 deste projeto.

Por que não usar `arduino-cli monitor` ou `screen`: nesta placa (bridge CH343),
abrir a porta com DTR/RTS ativos aciona o circuito de auto-reset e o ESP32
sobe em modo download ("waiting for download") em vez de rodar o sketch. Aqui
as duas linhas são zeradas logo após o open, e o reset (quando pedido) é um
pulso explícito só no EN.

Uso:
    python3 tools/serial_console.py            # conecta e reseta a placa
    python3 tools/serial_console.py --no-reset # conecta sem resetar

Digite os comandos do firmware e tecle Enter (o Enter não é enviado):
    i        troca para o barramento InfraVermelho
    r        troca para o barramento RF 433MHz
    #0..#9   grava o próximo código recebido no slot indicado
    0..9     reenvia o código gravado no slot
Ctrl-C ou Ctrl-D encerra.
"""
import argparse
import errno
import fcntl
import os
import select
import struct
import sys
import termios
import time

TIOCMBIS = 0x5416
TIOCMBIC = 0x5417
TIOCM_DTR = 0x002
TIOCM_RTS = 0x004


def open_port(dev):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    fcntl.ioctl(fd, TIOCMBIC, struct.pack("I", TIOCM_DTR | TIOCM_RTS))
    cc = list(termios.tcgetattr(fd)[6])
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, [
        0, 0,
        termios.CS8 | termios.CREAD | termios.CLOCAL,
        0,
        termios.B115200, termios.B115200,
        cc,
    ])
    fcntl.ioctl(fd, TIOCMBIC, struct.pack("I", TIOCM_DTR | TIOCM_RTS))
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def reset_board(fd):
    # DTR baixo mantém GPIO0 alto (boot normal); pulso em RTS reinicia via EN.
    fcntl.ioctl(fd, TIOCMBIC, struct.pack("I", TIOCM_DTR))
    fcntl.ioctl(fd, TIOCMBIS, struct.pack("I", TIOCM_RTS))
    time.sleep(0.15)
    termios.tcflush(fd, termios.TCIFLUSH)  # descarta ruído antes do boot
    fcntl.ioctl(fd, TIOCMBIC, struct.pack("I", TIOCM_RTS))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default="/dev/ttyACM0")
    ap.add_argument("--no-reset", action="store_true")
    ap.add_argument("--reset-only", action="store_true",
                    help="reinicia a placa e sai, sem abrir o console")
    args = ap.parse_args()

    if args.reset_only:
        fd = open_port(args.port)
        reset_board(fd)
        os.close(fd)
        print("[placa resetada]")
        return

    fd = open_port(args.port)
    print(f"[conectado em {args.port} @115200 — Ctrl-C para sair]")
    if not args.no_reset:
        reset_board(fd)
        print("[placa resetada]")

    stdin_fd = sys.stdin.fileno()
    pending = b""          # linha parcial vinda do stdin
    perdeu_porta = False
    try:
        while True:
            ready, _, _ = select.select([fd, stdin_fd], [], [], 0.1)
            if fd in ready:
                data = os.read(fd, 4096)
                if data:
                    sys.stdout.write(data.decode("utf-8", "replace"))
                    sys.stdout.flush()
            if stdin_fd in ready:
                # os.read cru (e não readline) para que o select enxergue tudo
                # que ainda não foi consumido — com pipe, o buffer do Python
                # engoliria as linhas seguintes e elas nunca seriam enviadas.
                chunk = os.read(stdin_fd, 4096)
                if not chunk:
                    break
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    cmd = line.strip()
                    if cmd:
                        os.write(fd, cmd)
    except KeyboardInterrupt:
        pass
    except OSError as exc:
        # EIO/ENODEV aqui não é erro de comando: a porta USB sumiu no meio da
        # sessão (a placa re-enumerou). Costuma ser queda de tensão no pico de
        # corrente do LED IR, cabo ruim ou a placa reiniciando sozinha.
        if exc.errno not in (errno.EIO, errno.ENODEV, errno.ENXIO):
            raise
        perdeu_porta = True
    finally:
        try:
            os.close(fd)
        except OSError:
            pass
        if perdeu_porta:
            print(f"\n[a porta {args.port} desapareceu — a placa re-enumerou]")
            print("[verifique o cabo/alimentacao e rode 'make ports' para reencontrar]")
        else:
            print("\n[desconectado]")

    if perdeu_porta:
        sys.exit(1)


if __name__ == "__main__":
    main()
