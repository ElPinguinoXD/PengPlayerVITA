#include "text_input.hpp"

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
bool g_initialized = false;

std::vector<SceWChar16> utf8ToUtf16(const std::string& text, std::size_t maxUnits) {
    std::vector<SceWChar16> out;
    out.reserve(std::min(maxUnits, text.size()) + 1);

    for (std::size_t i = 0; i < text.size() && out.size() < maxUnits;) {
        uint32_t cp = 0;
        unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;

        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = c & 0x07;
            extra = 3;
        } else {
            ++i;
            continue;
        }

        bool valid = true;
        for (std::size_t j = 1; j <= extra; ++j) {
            const unsigned char cc = static_cast<unsigned char>(text[i + j]);
            if ((cc & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += extra + 1;
        if (!valid) continue;

        if (cp <= 0xFFFF) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;
            out.push_back(static_cast<SceWChar16>(cp));
        } else if (cp <= 0x10FFFF && out.size() + 1 < maxUnits) {
            cp -= 0x10000;
            out.push_back(static_cast<SceWChar16>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<SceWChar16>(0xDC00 + (cp & 0x3FF)));
        }
    }

    out.push_back(0);
    return out;
}

std::string utf16ToUtf8(const SceWChar16* text) {
    std::string out;
    if (!text) return out;

    for (std::size_t i = 0; text[i] != 0; ++i) {
        uint32_t cp = text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + (((cp - 0xD800) << 10) | (text[++i] - 0xDC00));
        }

        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}
}

namespace TextInput {

bool initialize() {
    if (g_initialized) return true;

    // Algunos firmwares devuelven un error si el modulo ya estaba cargado;
    // no es fatal, por eso no abortamos por el valor de retorno.
    sceSysmoduleLoadModule(SCE_SYSMODULE_APPUTIL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);

    SceAppUtilInitParam initParam{};
    SceAppUtilBootParam bootParam{};
    if (sceAppUtilInit(&initParam, &bootParam) < 0) {
        // Puede estar inicializado por otra libreria. El dialogo dira si no.
    }

    SceCommonDialogConfigParam config{};
    sceCommonDialogSetConfigParam(&config);
    g_initialized = true;
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    sceAppUtilShutdown();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_APPUTIL);
    g_initialized = false;
}

std::string prompt(const std::string& title, const std::string& initialText, int maxLength) {
    if (!g_initialized) initialize();

    maxLength = std::max(1, std::min(maxLength, 128));
    std::vector<SceWChar16> title16 = utf8ToUtf16(title, 64);
    std::vector<SceWChar16> initial16 = utf8ToUtf16(initialText, static_cast<std::size_t>(maxLength));
    std::vector<SceWChar16> input(static_cast<std::size_t>(maxLength) + 1, 0);

    const std::size_t copyCount = std::min(initial16.size(), input.size());
    for (std::size_t i = 0; i < copyCount; ++i) input[i] = initial16[i];
    input.back() = 0;

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = 0x0001FFFFULL;
    param.languagesForced = SCE_TRUE;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.option = 0;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title = title16.data();
    param.maxTextLength = static_cast<SceUInt32>(maxLength);
    param.initialText = input.data();
    param.inputTextBuffer = input.data();

    if (sceImeDialogInit(&param) < 0) return "";

    bool accepted = false;
    bool finished = false;
    while (!finished) {
        const SceCommonDialogStatus status = sceImeDialogGetStatus();
        if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
            SceImeDialogResult result{};
            sceImeDialogGetResult(&result);
            accepted = result.button == SCE_IME_DIALOG_BUTTON_ENTER;
            sceImeDialogTerm();
            finished = true;
        } else {
            // vita2d ya tiene acceso al framebuffer y sync object actuales;
            // esta llamada dibuja/actualiza el teclado nativo encima de la app.
            vita2d_start_drawing();
            vita2d_clear_screen();
            vita2d_end_drawing();
            vita2d_common_dialog_update();
            vita2d_swap_buffers();
            sceKernelDelayThread(16000);
        }
    }

    return accepted ? utf16ToUtf8(input.data()) : std::string();
}

} // namespace TextInput
