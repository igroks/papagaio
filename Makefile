# Papagaio — controle universal IR + RF (ESP32)
#
# Atalhos para o fluxo de arduino-cli deste projeto.
# Rode `make` ou `make help` para ver os alvos disponíveis.

SKETCH      := firmware
FQBN        := esp32:esp32:esp32
BAUD        := 115200
ARDUINO_CLI := $(HOME)/.local/bin/arduino-cli
CONSOLE     := tools/serial_console.py

CORE_URL := https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
LIBS     := "IRremote@4.4.3" "rc-switch@2.6.4"

# Porta: detecta a primeira serial USB disponível; sobrescreva com `make upload PORT=/dev/ttyUSB0`
PORT ?= $(shell ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n1)

# A porta pertence ao grupo `dialout`. Se a sessão atual ainda não tiver esse
# grupo (usermod só vale no próximo login), reexecuta o comando via `sg`.
SG = $(shell if [ -w "$(PORT)" ] || id -nG | grep -qw dialout; then echo ""; else echo "sg dialout -c"; fi)

define run_on_port
	@if [ -z "$(PORT)" ]; then \
		echo "ERRO: nenhuma porta serial encontrada. Conecte a ESP32 (ou passe PORT=/dev/ttyXXX)."; \
		exit 1; \
	fi
	@if [ -n "$(SG)" ]; then sg dialout -c '$(1)'; else $(1); fi
endef

.DEFAULT_GOAL := help

.PHONY: help
help: ## Mostra esta ajuda
	@echo "Papagaio — controle universal IR + RF (ESP32)"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
		| awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Porta detectada: $(if $(PORT),$(PORT),<nenhuma — conecte a placa>)"

.PHONY: build
build: ## Compila o sketch (é o teste de correção deste projeto)
	$(ARDUINO_CLI) compile --fqbn $(FQBN) $(SKETCH)

.PHONY: upload
upload: ## Grava o firmware na ESP32 (compila antes)
	$(call run_on_port,$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(FQBN) $(SKETCH))

.PHONY: flash
flash: build upload ## Compila e grava numa tacada só

.PHONY: monitor
monitor: ## Abre o console serial interativo (reseta a placa ao conectar)
	$(call run_on_port,python3 $(CONSOLE) -p $(PORT))

.PHONY: attach
attach: ## Console serial SEM resetar (preserva um slot já armado com '#N')
	$(call run_on_port,python3 $(CONSOLE) -p $(PORT) --no-reset)

.PHONY: ports
ports: ## Lista as placas/portas seriais visíveis
	$(ARDUINO_CLI) board list

.PHONY: setup
setup: ## Instala o core ESP32 e as bibliotecas (IRremote, rc-switch)
	$(ARDUINO_CLI) config init --overwrite
	$(ARDUINO_CLI) config set board_manager.additional_urls $(CORE_URL)
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	$(ARDUINO_CLI) lib install $(LIBS)

.PHONY: deps
deps: ## Mostra as versões instaladas do core e das bibliotecas
	@$(ARDUINO_CLI) core list
	@$(ARDUINO_CLI) lib list IRremote rc-switch

.PHONY: reset
reset: ## Reinicia a ESP32 sem regravar
	$(call run_on_port,python3 $(CONSOLE) -p $(PORT) --reset-only)

.PHONY: clean
clean: ## Limpa os artefatos de build em cache do arduino-cli
	rm -rf $(HOME)/.cache/arduino/sketches
	@echo "cache de build limpo"
