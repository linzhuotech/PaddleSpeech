#include "base/type_conv.h"

namespace ppspeech {
// wstring to string
std::string wstring2utf8string(const std::wstring& wstr)
{
    static std::wstring_convert<std::codecvt_utf8<wchar_t> > strCnv;
    return strCnv.to_bytes(wstr);
}
 
// string to wstring 
std::wstring utf8string2wstring(const std::string& str)
{
    static std::wstring_convert< std::codecvt_utf8<wchar_t> > strCnv;
    return strCnv.from_bytes(str);
}

}
