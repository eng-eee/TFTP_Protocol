#pragma once
#include <iostream>


class Storage{
public:
    void Save(const std::string& data);
    std::string Load();
};
