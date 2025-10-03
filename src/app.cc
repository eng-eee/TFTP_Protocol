#include "app.h"
#include "StorageLayer/storage.h"

#include <iostream>


void App::Run() {
    Storage storage;
    storage.Save("Hello, World!");
    std::string data = storage.Load();
    std::cout << "Loaded data: " << data << std::endl;
    
    std::cout << "App is running!" << std::endl;
}