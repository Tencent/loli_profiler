#include "symboltranslator.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

// Run with a matching libUE4.so that has DWARF for this PC:
// SymbolizerInlineTest <llvm-symbolizer> <libUE4.so>
// The inline chain is TSet::Reserve -> TMapBase::Reserve ->
// UDataTable::LoadStructData. One captured PC should get the outer function.
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: SymbolizerInlineTest <llvm-symbolizer> <libUE4.so>\n";
        return 2;
    }
    constexpr uint64_t kPc = 0x129b22dc;
    std::unordered_map<uint64_t, std::string> symbols;
    const size_t count = loli::TranslateAddressesSymbolizer(
        argv[1], argv[2], std::vector<uint64_t>{kPc}, symbols);
    const std::string actual = symbols[kPc];
    if (count != 1 || actual.find("UDataTable::LoadStructData(") != 0) {
        std::cerr << "wrong outermost symbol for 0x129b22dc: " << actual << "\n";
        return 1;
    }
    std::cout << "0x129b22dc -> " << actual << "\n";
    return 0;
}
