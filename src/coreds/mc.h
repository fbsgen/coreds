#pragma once

#include <string>
#include <forward_list>

#include <flatbuffers/util.h>

namespace coreds {

void appendJsonStrTo(std::string& text, const std::string &src)
{
    const char* utf8;
    int ucc;
    char c;
    //text += '"';
    for (size_t i = 0, len = src.size(); i < len; i++)
    {
        switch ((c = src[i]))
        {
            case '\n': text += "\\n"; break;
            case '\t': text += "\\t"; break;
            case '\r': text += "\\r"; break;
            case '\b': text += "\\b"; break;
            case '\f': text += "\\f"; break;
            case '\"': text += "\\\""; break;
            case '\\': text += "\\\\"; break;
            default:
                if (c >= ' ' && c <= '~')
                {
                    text += c;
                }
                else
                {
                    // Not printable ASCII data. Let's see if it's valid UTF-8 first:
                    utf8 = src.data() + i;
                    ucc = flatbuffers::FromUTF8(&utf8);
                    if (ucc >= 0x80 && ucc <= 0xFFFF)
                    {
                        // Parses as Unicode within JSON's \uXXXX range, so use that.
                        text += "\\u";
                        text += flatbuffers::IntToStringHex(ucc, 4);
                        // Skip past characters recognized.
                        i = (utf8 - src.data() - 1);
                    }
                    else
                    {
                        // It's either unprintable ASCII, arbitrary binary, or Unicode data
                        // that doesn't fit \uXXXX, so use \xXX escape code instead.
                        text += "\\x";
                        text += flatbuffers::IntToStringHex(static_cast<uint8_t>(c), 2);
                    }
                }
                break;
        }
    }
    //text += '"';
}

struct MultiCAS
{
    static const int FN_BOOL = 1, 
            FN_BYTES = 2, 
            FN_STRING = 3, 
            FN_FLOAT = 4, 
            FN_DOUBLE = 5, 
            FN_UINT32 = 6, 
            FN_UINT64 = 7, 
            FN_INT32 = 8, // can be enum 
            FN_INT64 = 9, 
            FN_FIXED32 = 10, 
            FN_FIXED64 = 11, 
            FN_SINT32 = 12, 
            FN_SINT64 = 13, 
            FN_SFIXED32 = 14, 
            FN_SFIXED64 = 15;
    
private:
    std::forward_list<std::tuple<int,bool>> list_bool;
    std::forward_list<std::tuple<int,const std::string*,const std::string*>> list_bytes;
    std::forward_list<std::tuple<int,const std::string*,const std::string*>> list_string;
    //std::forward_list<std::tuple<int,float,float>> list_float;
    std::forward_list<std::tuple<int,double,double>> list_double;
    std::forward_list<std::tuple<int,unsigned,unsigned>> list_uint32;
    //std::forward_list<std::tuple<int,uint64_t,uint64_t>> list_uint64;
    //std::forward_list<std::tuple<int,int32_t,int32_t>> list_int32;
    //std::forward_list<std::tuple<int,int64_t,int64_t>> list_int64;
    std::forward_list<std::tuple<int,int32_t,int32_t>> list_fixed32;
    std::forward_list<std::tuple<int,int64_t,int64_t>> list_fixed64;
    //std::forward_list<std::tuple<int,int32_t,int32_t>> list_sint32;
    //std::forward_list<std::tuple<int,int64_t,int64_t>> list_sint64;
    //std::forward_list<std::tuple<int,int32_t,int32_t>> list_sfixed32;
    //std::forward_list<std::tuple<int,int64_t,int64_t>> list_sfixed64;
    
    int flags{ 0 };
public:
    bool empty()
    {
        return flags == 0;
    }
    MultiCAS& add(int f, bool newVal)
    {
        flags |= (1 << FN_BOOL);
        list_bool.emplace_front(f, newVal);
        return *this;
    }
    // pre-encoded with base64
    MultiCAS& addBytes(int f, const std::string* newVal, const std::string* oldVal)
    {
        flags |= (1 << FN_BYTES);
        list_bytes.emplace_front(f, newVal, oldVal);
        return *this;
    }
    MultiCAS& add(int f, const std::string* newVal, const std::string* oldVal)
    {
        flags |= (1 << FN_STRING);
        list_string.emplace_front(f, newVal, oldVal);
        return *this;
    }
    MultiCAS& add(int f, double newVal, double oldVal)
    {
        flags |= (1 << FN_DOUBLE);
        list_double.emplace_front(f, newVal, oldVal);
        return *this;
    }
    // var int
    MultiCAS& addUint32(int f, unsigned newVal, unsigned oldVal)
    {
        flags |= (1 << FN_UINT32);
        list_uint32.emplace_front(f, newVal, oldVal);
        return *this;
    }
    // 32
    MultiCAS& addFixed32(int f, int32_t newVal, int32_t oldVal)
    {
        flags |= (1 << FN_FIXED32);
        list_fixed32.emplace_front(f, newVal, oldVal);
        return *this;
    }
    MultiCAS& add(int f, int newVal, int oldVal)
    {
        return addFixed32(f, static_cast<int32_t>(newVal), static_cast<int32_t>(oldVal));
    }
    // 64
    MultiCAS& addFixed64(int f, int64_t newVal, int64_t oldVal)
    {
        flags |= (1 << FN_FIXED64);
        list_fixed64.emplace_front(f, newVal, oldVal);
        return *this;
    }
    MultiCAS& add(int f, int64_t newVal, int64_t oldVal)
    {
        return addFixed64(f, newVal, oldVal);
    }
    MultiCAS& add(int f, uint64_t newVal, uint64_t oldVal)
    {
        return addFixed64(f, static_cast<int64_t>(newVal), static_cast<int64_t>(oldVal));
    }
    MultiCAS& add(int f, int64_t newVal, uint64_t oldVal)
    {
        return addFixed64(f, newVal, static_cast<int64_t>(oldVal));
    }
    MultiCAS& add(int f, uint64_t newVal, int64_t oldVal)
    {
        return addFixed64(f, static_cast<int64_t>(newVal), oldVal);
    }
private:
    void bool_to(std::string& buf)
    {
        buf += R"("1":[)";
        do
        {
            auto& f = list_bool.front();
            buf += R"({"1":)";
            buf += std::to_string(std::get<0>(f));
            
            if (std::get<1>(f))
                buf += R"(,"2":false,"3":true})";
            else
                buf += R"(,"2":true,"3":false})";
            
            list_bool.pop_front();
            if (list_bool.empty())
                break;
            buf += ',';
        }
        while (true);
        buf += ']';
    }
    void bytes_to(std::string& buf)
    {
        buf += R"("2":[)";
        do
        {
            auto& f = list_bytes.front();
            buf += R"({"1":)";
            buf += std::to_string(std::get<0>(f));
            
            buf += R"(,"2":")";
            buf += *std::get<2>(f);
            
            buf += R"(","3":")";
            buf += *std::get<1>(f);
            
            buf += R"("})";
            list_bytes.pop_front();
            if (list_bytes.empty())
                break;
            buf += ',';
        }
        while (true);
        buf += ']';
    }
    void string_to(std::string& buf)
    {
        buf += R"("3":[)";
        do
        {
            auto& f = list_string.front();
            buf += R"({"1":)";
            buf += std::to_string(std::get<0>(f));
            
            buf += R"(,"2":")";
            appendJsonStrTo(buf, *std::get<2>(f));
            
            buf += R"(","3":")";
            appendJsonStrTo(buf, *std::get<1>(f));
            
            buf += R"("})";
            list_string.pop_front();
            if (list_string.empty())
                break;
            buf += ',';
        }
        while (true);
        buf += ']';
    }
    void double_to(std::string& buf)
    {
        buf += R"("5":[)";
        do
        {
            auto& f = list_double.front();
            buf += R"({"1":)";
            buf += std::to_string(std::get<0>(f));
            
            buf += R"(,"2":)";
            buf += std::to_string(std::get<2>(f));
            
            buf += R"(,"3":)";
            buf += std::to_string(std::get<1>(f));
            
            buf += '}';
            list_double.pop_front();
            if (list_double.empty())
                break;
            buf += ',';
        }
        while (true);
        buf += ']';
    }
    void uint32_to(std::string& buf)
    {
        buf += R"("6":[)";
        do
        {
            auto& f = list_uint32.front();
            buf += R"({"1":)";
            buf += std::to_string(std::get<0>(f));
            
            buf += R"(,"2":)";
            buf += std::to_string(std::get<2>(f));
            
            buf += R"(,"3":)";
            buf += std::to_string(std::get<1>(f));
            
            buf += '}';
            list_uint32.pop_front();
            if (list_uint32.empty())
                break;
            buf += ',';
        }
        while (true);
        buf += ']';
    }
    void fixed32_to(std::string& buf)
    {
        buf += R"("10":[)";
        do
        {
            auto& f = list_fixed32.front();
            buf += R"({"1":)";
            buf += std::to_string(std::get<0>(f));
            
            buf += R"(,"2":)";
            buf += std::to_string(std::get<2>(f));
            
            buf += R"(,"3":)";
            buf += std::to_string(std::get<1>(f));
            
            buf += '}';
            list_fixed32.pop_front();
            if (list_fixed32.empty())
                break;
            buf += ',';
        }
        while (true);
        buf += ']';
    }
    void fixed64_to(std::string& buf)
    {
        buf += R"("11":[)";
        do
        {
            auto& f = list_fixed64.front();
            buf += R"({"1":)";
            buf += std::to_string(std::get<0>(f));
            
            buf += R"(,"2":)";
            buf += std::to_string(std::get<2>(f));
            
            buf += R"(,"3":)";
            buf += std::to_string(std::get<1>(f));
            
            buf += '}';
            list_fixed64.pop_front();
            if (list_fixed64.empty())
                break;
            buf += ',';
        }
        while (true);
        buf += ']';
    }
public:
    void stringifyTo(std::string& buf)
    {
        if (flags == 0)
        {
            buf += "{}";
            return;
        }
        
        buf += '{';
        
        const auto sz = buf.size();
        
        if (!list_bool.empty())
        {
            bool_to(buf);
        }
        if (!list_bytes.empty())
        {
            if (sz != buf.size())
                buf += ',';
            
            bytes_to(buf);
        }
        if (!list_string.empty())
        {
            if (sz != buf.size())
                buf += ',';
            
            string_to(buf);
        }
        if (!list_double.empty())
        {
            if (sz != buf.size())
                buf += ',';
            
            double_to(buf);
        }
        if (!list_uint32.empty())
        {
            if (sz != buf.size())
                buf += ',';
            
            uint32_to(buf);
        }
        if (!list_fixed32.empty())
        {
            if (sz != buf.size())
                buf += ',';
            
            fixed32_to(buf);
        }
        if (!list_fixed64.empty())
        {
            if (sz != buf.size())
                buf += ',';
            
            fixed64_to(buf);
        }
        
        buf += '}';
    }
};

} // coreds
