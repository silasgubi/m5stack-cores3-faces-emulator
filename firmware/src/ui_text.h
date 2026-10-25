#pragma once
#include <stddef.h>

// Every on-screen string lives here. English by default; build with -DUI_LANG_PT_BR=1
// (env cores3-ptbr) for Brazilian Portuguese. Serial logs are not translated.
// Rules, checked at compile time at the end of this file:
// - ASCII only: the built-in fonts have no accented letters.
// - Text size 1 (6 px font) starts at x=8, so at most 51 chars fit in 320 px.
// - Text size 2 (12 px font) titles: at most 25 chars, the same room as a menu row.
// The web page of the file transfer mode has its own texts (web_page.h).

namespace ui {
constexpr size_t LINE_MAX_1 = 51;
constexpr size_t LINE_MAX_2 = 25;
constexpr size_t SPLASH_MAX = 17;     // text size 3 (18 px font)
constexpr size_t VALUE_MAX_2 = 19;    // size-2 values from x=80 (battery screen)
constexpr size_t MODE_MAX_2 = 21;     // size-2 charge mode between "< " and " >"
constexpr size_t FT_LABEL_MAX = 32;   // size-1 labels left of the QR code: 192 px from x=8
constexpr size_t FT_STATUS_MAX = 30;  // status formats, before the file name is inserted
}  // namespace ui

#if UI_LANG_PT_BR

constexpr char TXT_SPLASH_TITLE[] = "NES e Game Boy";
constexpr char TXT_SPLASH_SUB[] = "para M5Stack CoreS3";

constexpr char TXT_GAMES[] = "JOGOS";
constexpr char TXT_ON_CARD_FMT[] = "%d no cartao";  // printf format, one int
constexpr char TXT_NO_CARD[] = "sem cartao";
constexpr char TXT_MENU_FOOTER[] = "A/Start: jogar   Select: ajustes   B: ajuda";

constexpr char TXT_HELP[] = "AJUDA";
constexpr const char *TXT_HELP_LINES[] = {
    "ENERGIA   botao lateral: toque liga, ~4 s desliga",
    "",
    "NO JOGO   icones abaixo da tela:",
    "          < >  ajusta o controle ativo",
    "          o    volume > brilho > ambilight",
    "          dedo na tela por 1 s: mudo",
    "          Select+Start 1 s: pausa, salvar estado",
    "MENU      Select: ajustes (brilho, LEDs, volume,",
    "          modo de carga, enviar jogos por Wi-Fi)",
    "JOGOS     cartao /roms (.nes .gb .gbc) ou embutidos",
    "          estado salvo no cartao ou no console",
    "",
    "BATERIA   vermelho embaixo = carregar (chave em I)",
    "          USB-C do console: vale o modo de carga",
    "          base: sem modos, raio em 1 a 2 min",
};
constexpr char TXT_HELP_FOOTER[] = "Qualquer botao volta";

constexpr char TXT_NO_GAMES[] = "Nenhum jogo";
constexpr const char *TXT_NO_GAMES_LINES[] = {
    "Copie arquivos .nes, .gb ou .gbc para a pasta /roms",
    "de um cartao microSD FAT32, coloque o cartao e",
    "aperte outro botao para reiniciar.",
    "",
    "Select: ajustes e enviar jogos pelo Wi-Fi",
};

constexpr char TXT_LOADING[] = "Carregando...";
constexpr char TXT_READ_FAILED[] = "Nao consegui ler";
constexpr char TXT_GB_FAILED[] = "Game Boy falhou";
constexpr char TXT_NES_FAILED[] = "NES falhou";

// Pause menu (Select + Start in a game). Items in the order of pause_menu.cpp.
constexpr char TXT_PAUSED[] = "PAUSA";
constexpr const char *TXT_PAUSE_ITEMS[] = {"Continuar", "Salvar estado", "Carregar estado", "Sair para o menu"};
constexpr char TXT_PAUSE_ON_CARD[] = "Estado salvo no cartao";
constexpr char TXT_PAUSE_ON_CONSOLE[] = "Estado salvo no console";
constexpr char TXT_PAUSE_NO_STORAGE[] = "Sem cartao: nao da para salvar";
constexpr char TXT_PAUSE_NO_STATE[] = "Nenhum estado salvo ainda";
constexpr char TXT_PAUSE_SAVING[] = "Salvando...";
constexpr char TXT_PAUSE_SAVED[] = "Salvo";
constexpr char TXT_PAUSE_LOADED[] = "Carregado";
constexpr char TXT_PAUSE_FAILED[] = "Falhou";
constexpr char TXT_PAUSE_FOOTER[] = "A: escolher   B: voltar ao jogo";

// Settings (Select in the game menu). Items in the order of settings_menu.cpp.
constexpr char TXT_SETTINGS[] = "AJUSTES";
constexpr const char *TXT_SET_ITEMS[] = {"Bateria", "Brilho", "Ambilight", "Volume", "Transferir arquivos", "Ajuda"};
constexpr char TXT_SET_FOOTER[] = "A: abrir    < >: mudar    B: voltar";

// Battery screen. Charge modes in the order of power_mode.h.
constexpr char TXT_BATTERY[] = "BATERIA";
constexpr char TXT_BAT_LEVEL[] = "Carga";
constexpr char TXT_BAT_VOLTAGE[] = "Tensao";
constexpr char TXT_BAT_STATE[] = "Estado";
constexpr char TXT_BAT_CURRENT[] = "Corrente";
constexpr char TXT_BAT_CHARGING[] = "Carregando";
constexpr char TXT_BAT_CHARGING_BASE[] = "Carregando (base)";
constexpr char TXT_BAT_ON_BATTERY[] = "Na bateria";
constexpr char TXT_BAT_NOT_CHARGING[] = "Sem carregar";
constexpr char TXT_BAT_PAUSED[] = "Carga pausada";
constexpr char TXT_BAT_CHARGE_OFF[] = "Carga desligada";
constexpr char TXT_BAT_ABSENT[] = "Sem bateria";
constexpr char TXT_BAT_ABSENT_HINT[] = "A chave traseira da base esta em O.";
constexpr char TXT_BAT_MODE[] = "Modo de carga";
constexpr const char *TXT_CHARGE_MODES[] = {"Normal", "Rapida", "Inteligente 40-80%", "Desligada"};
constexpr const char *TXT_CHARGE_MODE_INFO[] = {
    "200 mA ate 100% (padrao de fabrica)",
    "400 mA ate 100%, perto de 2x mais rapida",
    "Para em ~80% e volta a carregar abaixo de ~40%",
    "Nao carrega pelo USB-C do console",
};
constexpr const char *TXT_BAT_NOTE_LINES[] = {
    "O modo vale so para a carga pelo USB-C do console.",
    "A base de carga magnetica sempre carrega ate 100%.",
};
constexpr char TXT_BAT_FOOTER[] = "< >: modo de carga    B: voltar";

// File transfer (wifi_transfer.cpp).
constexpr char TXT_FT_TITLE[] = "TRANSFERIR ARQUIVOS";
constexpr char TXT_FT_NETWORK[] = "Rede Wi-Fi";
constexpr char TXT_FT_PASSWORD[] = "Senha";
constexpr char TXT_FT_OPEN[] = "Depois abra no navegador";
constexpr char TXT_FT_SCAN[] = "Ou aponte a camera do celular para o QR code.";
constexpr char TXT_FT_WAITING[] = "Esperando celular ou computador...";
constexpr char TXT_FT_CLIENTS_FMT[] = "%d conectado(s)";  // printf format, one int
constexpr char TXT_FT_FOOTER[] = "B: sair";
// Status line formats: %s = file name (shortened to fit), %u = number.
constexpr char TXT_FT_RECEIVING_FMT[] = "Recebendo %s %u%%";
constexpr char TXT_FT_UPLOADED_FMT[] = "Recebido %s %u KB";
constexpr char TXT_FT_REJECTED_FMT[] = "Recusado %s";
constexpr char TXT_FT_SAVE_FAILED_FMT[] = "Nao consegui gravar %s";
constexpr char TXT_FT_INTERRUPTED_FMT[] = "Interrompido %s";
constexpr char TXT_FT_DELETED_FMT[] = "Apagado %s";
constexpr char TXT_FT_DOWNLOADED_FMT[] = "Baixado %s";
constexpr char TXT_FT_NO_CARD[] = "Sem cartao microSD";
constexpr const char *TXT_FT_NO_CARD_LINES[] = {
    "Coloque um cartao microSD FAT32 e aperte",
    "qualquer botao para reiniciar.",
};
constexpr char TXT_FT_WIFI_FAILED[] = "Wi-Fi falhou";
constexpr const char *TXT_FT_WIFI_FAILED_LINES[] = {
    "Nao consegui criar a rede do console.",
    "Aperte qualquer botao para reiniciar.",
};

#else  // English

constexpr char TXT_SPLASH_TITLE[] = "NES & Game Boy";
constexpr char TXT_SPLASH_SUB[] = "for M5Stack CoreS3";

constexpr char TXT_GAMES[] = "GAMES";
constexpr char TXT_ON_CARD_FMT[] = "%d on card";  // printf format, one int
constexpr char TXT_NO_CARD[] = "no card";
constexpr char TXT_MENU_FOOTER[] = "A/Start: play   Select: settings   B: help";

constexpr char TXT_HELP[] = "HELP";
constexpr const char *TXT_HELP_LINES[] = {
    "POWER     side button: tap = on, hold ~4 s = off",
    "",
    "IN GAME   touch the icons below the screen:",
    "          < >  adjust the active control",
    "          o    volume > brightness > ambilight",
    "          finger on the screen for 1 s: mute",
    "          Select+Start 1 s: pause, save/load state",
    "MENU      Select: settings (brightness, LEDs,",
    "          volume, charge mode, Wi-Fi transfer)",
    "GAMES     card /roms (.nes .gb .gbc) or built in",
    "          states go to the card or the console",
    "",
    "BATTERY   red bottom LEDs = charge (rear switch I)",
    "          console USB-C: charge modes apply",
    "          base/dock: no modes, bolt after 1-2 min",
};
constexpr char TXT_HELP_FOOTER[] = "Any button goes back";

constexpr char TXT_NO_GAMES[] = "No games found";
constexpr const char *TXT_NO_GAMES_LINES[] = {
    "Copy .nes, .gb or .gbc files to the /roms folder",
    "of a FAT32 microSD card, insert it and press any",
    "other button to restart.",
    "",
    "Select: settings and Wi-Fi file transfer",
};

constexpr char TXT_LOADING[] = "Loading...";
constexpr char TXT_READ_FAILED[] = "Could not read";
constexpr char TXT_GB_FAILED[] = "Game Boy failed";
constexpr char TXT_NES_FAILED[] = "NES failed";

// Pause menu (Select + Start in a game). Items in the order of pause_menu.cpp.
constexpr char TXT_PAUSED[] = "PAUSED";
constexpr const char *TXT_PAUSE_ITEMS[] = {"Continue", "Save state", "Load state", "Quit to menu"};
constexpr char TXT_PAUSE_ON_CARD[] = "State saved on the card";
constexpr char TXT_PAUSE_ON_CONSOLE[] = "State saved on the console";
constexpr char TXT_PAUSE_NO_STORAGE[] = "No card: cannot save";
constexpr char TXT_PAUSE_NO_STATE[] = "No saved state yet";
constexpr char TXT_PAUSE_SAVING[] = "Saving...";
constexpr char TXT_PAUSE_SAVED[] = "Saved";
constexpr char TXT_PAUSE_LOADED[] = "Loaded";
constexpr char TXT_PAUSE_FAILED[] = "Failed";
constexpr char TXT_PAUSE_FOOTER[] = "A: choose   B: back to the game";

// Settings (Select in the game menu). Items in the order of settings_menu.cpp.
constexpr char TXT_SETTINGS[] = "SETTINGS";
constexpr const char *TXT_SET_ITEMS[] = {"Battery", "Brightness", "Ambilight", "Volume", "File transfer", "Help"};
constexpr char TXT_SET_FOOTER[] = "A: open    < >: change    B: back";

// Battery screen. Charge modes in the order of power_mode.h.
constexpr char TXT_BATTERY[] = "BATTERY";
constexpr char TXT_BAT_LEVEL[] = "Level";
constexpr char TXT_BAT_VOLTAGE[] = "Voltage";
constexpr char TXT_BAT_STATE[] = "State";
constexpr char TXT_BAT_CURRENT[] = "Current";
constexpr char TXT_BAT_CHARGING[] = "Charging";
constexpr char TXT_BAT_CHARGING_BASE[] = "Charging (base)";
constexpr char TXT_BAT_ON_BATTERY[] = "On battery";
constexpr char TXT_BAT_NOT_CHARGING[] = "Not charging";
constexpr char TXT_BAT_PAUSED[] = "Charge paused";
constexpr char TXT_BAT_CHARGE_OFF[] = "Charging off";
constexpr char TXT_BAT_ABSENT[] = "No battery";
constexpr char TXT_BAT_ABSENT_HINT[] = "The rear switch of the base is on O.";
constexpr char TXT_BAT_MODE[] = "Charge mode";
constexpr const char *TXT_CHARGE_MODES[] = {"Normal", "Fast", "Smart 40-80%", "Off"};
constexpr const char *TXT_CHARGE_MODE_INFO[] = {
    "200 mA up to 100% (factory setting)",
    "400 mA up to 100%, about twice as fast",
    "Stops at ~80%, charges again below ~40%",
    "Does not charge from the console USB-C",
};
constexpr const char *TXT_BAT_NOTE_LINES[] = {
    "The mode only applies to the console USB-C.",
    "The magnetic charging base always charges to 100%.",
};
constexpr char TXT_BAT_FOOTER[] = "< >: charge mode    B: back";

// File transfer (wifi_transfer.cpp).
constexpr char TXT_FT_TITLE[] = "FILE TRANSFER";
constexpr char TXT_FT_NETWORK[] = "Wi-Fi network";
constexpr char TXT_FT_PASSWORD[] = "Password";
constexpr char TXT_FT_OPEN[] = "Then open in the browser";
constexpr char TXT_FT_SCAN[] = "Or scan the QR code with the phone camera.";
constexpr char TXT_FT_WAITING[] = "Waiting for a phone or computer...";
constexpr char TXT_FT_CLIENTS_FMT[] = "%d connected";  // printf format, one int
constexpr char TXT_FT_FOOTER[] = "B: exit";
// Status line formats: %s = file name (shortened to fit), %u = number.
constexpr char TXT_FT_RECEIVING_FMT[] = "Receiving %s %u%%";
constexpr char TXT_FT_UPLOADED_FMT[] = "Uploaded %s %u KB";
constexpr char TXT_FT_REJECTED_FMT[] = "Rejected %s";
constexpr char TXT_FT_SAVE_FAILED_FMT[] = "Could not save %s";
constexpr char TXT_FT_INTERRUPTED_FMT[] = "Interrupted %s";
constexpr char TXT_FT_DELETED_FMT[] = "Deleted %s";
constexpr char TXT_FT_DOWNLOADED_FMT[] = "Downloaded %s";
constexpr char TXT_FT_NO_CARD[] = "No microSD card";
constexpr const char *TXT_FT_NO_CARD_LINES[] = {
    "Insert a FAT32 microSD card and press any",
    "button to restart.",
};
constexpr char TXT_FT_WIFI_FAILED[] = "Wi-Fi failed";
constexpr const char *TXT_FT_WIFI_FAILED_LINES[] = {
    "Could not start the console network.",
    "Press any button to restart.",
};

#endif

#define UI_COUNT(a) (sizeof(a) / sizeof((a)[0]))
constexpr size_t TXT_HELP_COUNT = UI_COUNT(TXT_HELP_LINES);
constexpr size_t TXT_NO_GAMES_COUNT = UI_COUNT(TXT_NO_GAMES_LINES);
constexpr size_t TXT_SET_ITEM_COUNT = UI_COUNT(TXT_SET_ITEMS);
constexpr size_t TXT_CHARGE_MODE_COUNT = UI_COUNT(TXT_CHARGE_MODES);
constexpr size_t TXT_BAT_NOTE_COUNT = UI_COUNT(TXT_BAT_NOTE_LINES);
constexpr size_t TXT_PAUSE_ITEM_COUNT = UI_COUNT(TXT_PAUSE_ITEMS);
constexpr size_t TXT_FT_NO_CARD_COUNT = UI_COUNT(TXT_FT_NO_CARD_LINES);
constexpr size_t TXT_FT_WIFI_FAILED_COUNT = UI_COUNT(TXT_FT_WIFI_FAILED_LINES);

// Compile-time layout check (C++11 constexpr, so recursion instead of loops).
namespace ui {
constexpr size_t len(const char *s) { return *s ? 1 + len(s + 1) : 0; }
constexpr bool ascii(const char *s) { return !*s || ((unsigned char)*s < 0x80 && ascii(s + 1)); }
constexpr bool fits(const char *s, size_t max) { return len(s) <= max && ascii(s); }
constexpr bool allFit(const char *const *l, size_t n, size_t max) {
    return n == 0 || (fits(*l, max) && allFit(l + 1, n - 1, max));
}
}  // namespace ui

static_assert(ui::fits(TXT_SPLASH_TITLE, ui::SPLASH_MAX), "splash title too long or not ASCII");
static_assert(ui::fits(TXT_SPLASH_SUB, ui::LINE_MAX_2), "splash subtitle too long or not ASCII");
static_assert(ui::fits(TXT_GAMES, ui::LINE_MAX_2) && ui::fits(TXT_HELP, ui::LINE_MAX_2) &&
                  ui::fits(TXT_NO_GAMES, ui::LINE_MAX_2) && ui::fits(TXT_LOADING, ui::LINE_MAX_2) &&
                  ui::fits(TXT_READ_FAILED, ui::LINE_MAX_2) && ui::fits(TXT_GB_FAILED, ui::LINE_MAX_2) &&
                  ui::fits(TXT_NES_FAILED, ui::LINE_MAX_2) && ui::fits(TXT_SETTINGS, ui::LINE_MAX_2) &&
                  ui::fits(TXT_BATTERY, ui::LINE_MAX_2),
              "size-2 title too long or not ASCII");
// The game menu header also holds the battery (percent and icon) at the right: the
// counter must end before it (menu.cpp, HEADER_TEXT_END).
static_assert(ui::fits(TXT_ON_CARD_FMT, 14) && ui::fits(TXT_NO_CARD, 14), "header counter too long or not ASCII");
static_assert(ui::fits(TXT_MENU_FOOTER, ui::LINE_MAX_1) && ui::fits(TXT_HELP_FOOTER, ui::LINE_MAX_1) &&
                  ui::fits(TXT_SET_FOOTER, ui::LINE_MAX_1) && ui::fits(TXT_BAT_FOOTER, ui::LINE_MAX_1),
              "footer too long or not ASCII");
static_assert(ui::allFit(TXT_HELP_LINES, TXT_HELP_COUNT, ui::LINE_MAX_1), "help line too long or not ASCII");
static_assert(TXT_HELP_COUNT <= 15, "help lines would run into the footer");
static_assert(ui::allFit(TXT_NO_GAMES_LINES, TXT_NO_GAMES_COUNT, ui::LINE_MAX_1),
              "no-games line too long or not ASCII");

// Pause box (pause_menu.cpp): 240 px wide, text from 12 px inside, so 18 chars at size 2
// and 36 at size 1.
static_assert(TXT_PAUSE_ITEM_COUNT == 4, "pause_menu.cpp expects 4 items");
static_assert(ui::fits(TXT_PAUSED, 18) && ui::allFit(TXT_PAUSE_ITEMS, TXT_PAUSE_ITEM_COUNT, 18),
              "pause title or item too long or not ASCII");
static_assert(ui::fits(TXT_PAUSE_ON_CARD, 36) && ui::fits(TXT_PAUSE_ON_CONSOLE, 36) &&
                  ui::fits(TXT_PAUSE_NO_STORAGE, 36) && ui::fits(TXT_PAUSE_NO_STATE, 36) &&
                  ui::fits(TXT_PAUSE_SAVING, 36) && ui::fits(TXT_PAUSE_SAVED, 36) &&
                  ui::fits(TXT_PAUSE_LOADED, 36) && ui::fits(TXT_PAUSE_FAILED, 36) &&
                  ui::fits(TXT_PAUSE_FOOTER, 36),
              "pause line too long or not ASCII");

// Settings rows are size 2 from x=8; the right part of the row holds the value.
static_assert(TXT_SET_ITEM_COUNT == 6, "settings_menu.cpp expects 6 items");
static_assert(ui::allFit(TXT_SET_ITEMS, TXT_SET_ITEM_COUNT, 20), "settings item too long or not ASCII");
static_assert(ui::fits(TXT_BAT_LEVEL, 12) && ui::fits(TXT_BAT_VOLTAGE, 12) && ui::fits(TXT_BAT_STATE, 12) &&
                  ui::fits(TXT_BAT_CURRENT, 12) && ui::fits(TXT_BAT_MODE, ui::LINE_MAX_1),
              "battery label too long or not ASCII");
static_assert(ui::fits(TXT_BAT_CHARGING, ui::VALUE_MAX_2) && ui::fits(TXT_BAT_CHARGING_BASE, ui::VALUE_MAX_2) && ui::fits(TXT_BAT_ON_BATTERY, ui::VALUE_MAX_2) &&
                  ui::fits(TXT_BAT_NOT_CHARGING, ui::VALUE_MAX_2) && ui::fits(TXT_BAT_PAUSED, ui::VALUE_MAX_2) &&
                  ui::fits(TXT_BAT_CHARGE_OFF, ui::VALUE_MAX_2) && ui::fits(TXT_BAT_ABSENT, ui::VALUE_MAX_2),
              "battery state too long or not ASCII");
static_assert(ui::fits(TXT_BAT_ABSENT_HINT, ui::LINE_MAX_1), "battery hint too long or not ASCII");
static_assert(TXT_CHARGE_MODE_COUNT == 4 && UI_COUNT(TXT_CHARGE_MODE_INFO) == 4,
              "one name and one line per charge mode (power_mode.h)");
static_assert(ui::allFit(TXT_CHARGE_MODES, TXT_CHARGE_MODE_COUNT, ui::MODE_MAX_2),
              "charge mode name too long or not ASCII");
static_assert(ui::allFit(TXT_CHARGE_MODE_INFO, TXT_CHARGE_MODE_COUNT, ui::LINE_MAX_1),
              "charge mode line too long or not ASCII");
static_assert(ui::allFit(TXT_BAT_NOTE_LINES, TXT_BAT_NOTE_COUNT, ui::LINE_MAX_1), "battery note too long or not ASCII");

static_assert(ui::fits(TXT_FT_TITLE, ui::LINE_MAX_2) && ui::fits(TXT_FT_NO_CARD, ui::LINE_MAX_2) &&
                  ui::fits(TXT_FT_WIFI_FAILED, ui::LINE_MAX_2),
              "file transfer title too long or not ASCII");
static_assert(ui::fits(TXT_FT_NETWORK, ui::FT_LABEL_MAX) && ui::fits(TXT_FT_PASSWORD, ui::FT_LABEL_MAX) &&
                  ui::fits(TXT_FT_OPEN, ui::FT_LABEL_MAX),
              "file transfer label too long or not ASCII");
static_assert(ui::fits(TXT_FT_SCAN, ui::LINE_MAX_1) && ui::fits(TXT_FT_WAITING, ui::LINE_MAX_1) &&
                  ui::fits(TXT_FT_CLIENTS_FMT, 20) && ui::fits(TXT_FT_FOOTER, 20),
              "file transfer line too long or not ASCII");
static_assert(ui::fits(TXT_FT_RECEIVING_FMT, ui::FT_STATUS_MAX) && ui::fits(TXT_FT_UPLOADED_FMT, ui::FT_STATUS_MAX) &&
                  ui::fits(TXT_FT_REJECTED_FMT, ui::FT_STATUS_MAX) &&
                  ui::fits(TXT_FT_SAVE_FAILED_FMT, ui::FT_STATUS_MAX) &&
                  ui::fits(TXT_FT_INTERRUPTED_FMT, ui::FT_STATUS_MAX) &&
                  ui::fits(TXT_FT_DELETED_FMT, ui::FT_STATUS_MAX) && ui::fits(TXT_FT_DOWNLOADED_FMT, ui::FT_STATUS_MAX),
              "file transfer status format too long or not ASCII");
static_assert(ui::allFit(TXT_FT_NO_CARD_LINES, TXT_FT_NO_CARD_COUNT, ui::LINE_MAX_1) &&
                  ui::allFit(TXT_FT_WIFI_FAILED_LINES, TXT_FT_WIFI_FAILED_COUNT, ui::LINE_MAX_1),
              "file transfer message line too long or not ASCII");
