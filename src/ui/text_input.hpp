#pragma once

#include <string>

namespace TextInput {

bool initialize();
void shutdown();

// Muestra el teclado nativo de PS Vita. Devuelve una cadena vacia si el
// usuario cancela o si no se pudo abrir el dialogo.
std::string prompt(const std::string& title, const std::string& initialText = "", int maxLength = 48);

} // namespace TextInput
