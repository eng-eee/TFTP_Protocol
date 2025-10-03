#include "storage.h"

void Storage::Save(const std::string& data) {
    // Veriyi kaydetme işlemi (örnek olarak konsola yazdırma)
    std::cout << "Saving data: " << data << std::endl;
}
std::string Storage::Load() {
    // Veriyi yükleme işlemi (örnek olarak sabit bir veri döndürme)
    return "Loaded data";
}
