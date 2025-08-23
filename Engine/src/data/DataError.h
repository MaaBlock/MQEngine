//
// Created by Administrator on 2025/8/20.
//

#ifndef DATAERROR_H
#define DATAERROR_H

#include <stdexcept>
#include <string>

namespace MQEngine {
    
    class ENGINE_API DataError : public std::runtime_error
    {
    public:
        explicit DataError(const std::string& message) 
            : std::runtime_error(message) {}
        
        explicit DataError(const char* message) 
            : std::runtime_error(message) {}
        
        virtual ~DataError() = default;
    };
    
} // namespace MQEngine

#endif //DATAERROR_H
