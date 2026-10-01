#include <iostream>
#include <string_view>

extern "C" {
    __declspec(dllimport) int parse_email_address(
        const wchar_t* rawInput,
        wchar_t** outDisplayName,
        wchar_t** outUser,
        wchar_t** outHost,
        wchar_t** outAddress
    );

    __declspec(dllimport) void free_email_string(wchar_t* ptr);
}

struct ScopedNativeString {
    wchar_t* ptr = nullptr;
    ~ScopedNativeString() { if (ptr) free_email_string(ptr); }
    std::wstring_view view() const { return ptr ? ptr : L""; }
};

int main() {
    const wchar_t* input = L"\"Ackermann, Refael\" <user.name+tag@example.com>";

    ScopedNativeString name, user, host, fullAddress;

    int rc = parse_email_address(
        input,
        &name.ptr,
        &user.ptr,
        &host.ptr,
        &fullAddress.ptr
    );

    if (rc == 0) {
        std::wcout << L"Valid RFC Address:\n";
        std::wcout << L"  Display: " << name.view() << L"\n";
        std::wcout << L"  User:    " << user.view() << L"\n";
        std::wcout << L"  Host:    " << host.view() << L"\n";
        std::wcout << L"  Address: " << fullAddress.view() << L"\n";
    } else {
        std::wcout << L"Invalid format (code: " << rc << L")\n";
    }

    return rc;
}