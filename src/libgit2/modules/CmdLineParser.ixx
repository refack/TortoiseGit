export module CmdLineParser;
import std;
import str_utils;

using std::map;


namespace TCmdLineParser
{
    /**
     * \ingroup Utils
     *
     * A helper class for parsing command lines.
     * It provides methods to extract 'key' and 'value'
     * pairs of the form -keyname:value or /keyname:value.
     * Parameter examples:\n
     * \code
     * "/key1 /key2:myvalue -key3:anothervalue -key4:"this could be a path with spaces"
     * \endcode
     * /key is the same as -key\n
     * all keys and values are case-insensitive.
     * Please note that both keys and values are strings although the class
     * provides a method to get a long as a value.
     * Example:\n
     * \code
     * CCmdLineParser parser(::GetCommandLine());
     * if (parser.HasKey("r"))
     * {
     * 	// the key -r or /r is there (could mean e.g. 'recurse')
     * }
     * //now assume the command line is /open:"c:\test.txt" /wait:30
     * CString file = parser.GetVal("open");
     * //file contains now c:\test.txt
     * long number = parser.GetLongVal("seconds");
     * //number has now the value 30
     * \endcode
     */
    class CCmdLineParser
    {
    public:
        using CValsMap = map<std::wstring, std::wstring>;
        using ITERPOS = CValsMap::const_iterator;

        /**
         * Creates a CCmdLineParser object and parses the parameters in.
         * \param sCmdLine the command line
         */
        CCmdLineParser(const wchar_t* sCmdLine = nullptr);

        CCmdLineParser(const CCmdLineParser&) = default;
        CCmdLineParser& operator=(const CCmdLineParser&) = default;
        CCmdLineParser& operator=(CCmdLineParser&& other) noexcept;

        /**
         * returns the command line string this object was created on.
         * \return the command line
         */
        [[nodiscard]] const wchar_t* getCmdLine() const { return m_sCmdLine.c_str(); }

        /**
         * Starts an iteration over all command line parameters.
         * \return the first position
         */
        [[nodiscard]] ITERPOS begin() const;

        /**
         * Get the next key/value pair. If no more keys are available then
         * an empty key is returned.
         * \param pos the position from where to get. To get the first pair use the
         * begin() method. \a pos is incremented by 1 on return.
         * \param sKey returns the key
         * \param sValue returns the value
         * \return the next position
         */
        ITERPOS getNext(ITERPOS& pos, std::wstring& sKey, std::wstring& sValue) const;

        /**
         * Checks if the position is the last or if there are more key/value pairs in the command line.
         * \param pos the position to check
         * \return TRUE if no more key/value pairs are available
         */
        [[nodiscard]] bool isLast(const ITERPOS& pos) const;

        /**
         * Checks if the given key is in the command line.
         * \param sKey the key to check for
         * \return TRUE if the key exists, FALSE if the key is not in command line
         */
        bool HasKey(const wchar_t* sKey) const;

        /**
         * Checks if a key also has a value or not.
         * \param sKey the key to check for a value
         * \return TRUE if the key has a value, FALSE if no value (or no key) was found
         */
        bool HasVal(const wchar_t* sKey) const;

        /**
         * Reads the value for a key. If the key has no value then nullptr is returned.
         * \param sKey the key to get the value from
         * \return the value string of the key
         */
        const wchar_t* GetVal(const wchar_t* sKey) const;

        /**
         * Reads the value for a key as an owning string. A key that is not there
         * yields an empty string.
         *
         * This exists because GetVal's nullptr cannot safely be handed to a
         * std::wstring: CString's constructor reads a null pointer as an empty
         * string, std::wstring's is undefined behaviour, and that difference turned
         * "TortoiseGitProc /command:diff" - which reaches for an optional /path2 on
         * every invocation - into an access violation. GetVal itself keeps returning
         * nullptr, because CAppUtils::CreateBranchTag reads it as "no name given".
         * \param sKey the key to get the value from
         * \return the value of the key, or an empty string if it has none
         */
        [[nodiscard]] std::wstring GetValStr(const wchar_t* sKey) const;

        /**
         * Reads the value for a key as a long. If the value is a string which can't be
         * converted to a number then 0 is returned.
         * \param sKey the key to get the value from
         * \return the value converted to a long
         */
        long GetLongVal(const wchar_t* sKey) const;

        __int64 GetLongLongVal(const wchar_t* sKey) const;

        [[nodiscard]] static std::wstring EscapeValue(std::wstring_view input);
        // CString is available in every compile now (the str_utils shim), so this is not
        // behind CSTRING_AVAILABLE any more. Constrained, not a plain CString parameter:
        // EscapeValue(L"...") would otherwise be ambiguous with the wstring_view one.
        template <tgit::str::IsStupidString TStr>
        [[nodiscard]] static TStr EscapeValue(TStr input)
        {
            input.Replace(L"\"", L"\"\"");
            return L'"' + input + L'"';
        }

    private:
        bool Parse(const wchar_t* sCmdLine);
        ITERPOS findKey(const wchar_t* sKey) const;
        [[nodiscard]] const CValsMap& getVals() const { return m_valueMap; }

        std::wstring m_sCmdLine;
        CValsMap m_valueMap;

        static const wchar_t m_sDelims[];
        static const wchar_t m_sValueSep[];
        static const wchar_t m_sQuotes[];
    };


    constexpr wchar_t CCmdLineParser::m_sDelims[] = L"-/";
    constexpr wchar_t CCmdLineParser::m_sQuotes[] = L"\"";
    constexpr wchar_t CCmdLineParser::m_sValueSep[] = L" :"; // don't forget space!!


    CCmdLineParser::CCmdLineParser(const wchar_t* sCmdLine)
    {
        if (sCmdLine)
            Parse(sCmdLine);
    }

    CCmdLineParser& CCmdLineParser::operator=(CCmdLineParser&& other) noexcept
    {
        m_sCmdLine = std::move(other.m_sCmdLine);
        m_valueMap = std::move(other.m_valueMap);
        return *this;
    }

    bool CCmdLineParser::Parse(const wchar_t* sCmdLine)
    {
	    if (!sCmdLine)
            return false;

        m_valueMap.clear();
        m_sCmdLine = sCmdLine;

        std::wstring working = sCmdLine;
        const wchar_t* sCurrent = working.data();

        for (;;)
        {
            //format is  -Key:"arg"

            if (!sCurrent[0])
                break; // no more data, leave loop

            const wchar_t* sArg = std::wcspbrk(sCurrent, m_sDelims);
            if (!sArg)
                break; // no (more) delimiters found
            ++sArg;

            if (!sArg[0])
                break; // ends with delim

            const wchar_t* sVal = std::wcspbrk(sArg, m_sValueSep);
            if (!sVal)
            {
	            const std::wstring sEmpty;
	            std::wstring Key(sArg);
                std::ranges::transform(Key, Key.begin(), std::towlower);
                m_valueMap.insert(CValsMap::value_type(Key, sEmpty));
                break;
            }
            else
            {
                std::wstring Key(sArg, static_cast<int>(sVal - sArg));
                std::ranges::transform(Key, Key.begin(), std::towlower);

                const wchar_t* sQuote = nullptr;
                const wchar_t* sEndQuote = nullptr;
                if (sVal[0])
                {
                    if (sVal[0] != L' ')
                        ++sVal;
                    else
                    {
                        while (sVal[0] == L' ')
                            ++sVal;
                    }

                    const wchar_t* nextArg = std::wcspbrk(sVal, m_sDelims);

                    sQuote = std::wcspbrk(sVal, m_sQuotes);

                    if (nextArg == sVal)
                    {
                        // current key has no value, but a next key exist - so don't use next key as value of current one
                        --sVal;
                        sQuote = sVal;
                        sEndQuote = sVal;
                    }
                    else if (nextArg && nextArg < sQuote)
                    {
                        // current key has a value w/o quotes, but next key one has value in quotes
                        sQuote = sVal;
                        sEndQuote = std::wcschr(sQuote, L' ');
                    }
                    else
                    {
                        if (sQuote == sVal)
                        {
                            // string with quotes (defined in m_sQuotes, e.g. '")
                            sQuote = sVal + 1;
                            sEndQuote = std::wcspbrk(sQuote, m_sQuotes);

                            // search for double quotes
                            while (sEndQuote)
                            {
                                auto nextQuote = sEndQuote + 1;
                                if (nextQuote[0] == L'"')
                                {
                                    working.erase(working.begin() + (sEndQuote - working.data()));
                                    sEndQuote = std::wcspbrk(nextQuote, m_sQuotes);
                                    continue;
                                }
                                break;
                            }
                        }
                        else
                        {
                            sQuote = sVal;
                            sEndQuote = std::wcschr(sQuote, L' ');
                        }
                    }
                }

                if (!sEndQuote)
                {
                    // no end quotes or terminating space, take the rest of the string to its end
                    if (!Key.empty() && sQuote)
                    {
                        std::wstring csVal(sQuote);
                        m_valueMap.insert(CValsMap::value_type(Key, csVal));
                    }
                    break;
                }
                else
                {
                    // end quote
                    if (!Key.empty())
                    {
                        std::wstring csVal(sQuote, static_cast<int>(sEndQuote - sQuote));
                        m_valueMap.insert(CValsMap::value_type(Key, csVal));
                    }
                    sCurrent = sEndQuote + 1;
                    continue;
                }
            }
        }

        return !m_valueMap.empty(); //TRUE if arguments were found
    }

    CCmdLineParser::ITERPOS CCmdLineParser::findKey(const wchar_t* sKey) const
    {
        std::wstring s(sKey);
        std::ranges::transform(s, s.begin(), std::towlower);
        return m_valueMap.find(s);
    }

    bool CCmdLineParser::HasKey(const wchar_t* sKey) const
    {
        const auto it = findKey(sKey);
        if (it == m_valueMap.cend())
            return false;
        return true;
    }


    bool CCmdLineParser::HasVal(const wchar_t* sKey) const
    {
        const auto it = findKey(sKey);
        if (it == m_valueMap.cend())
            return false;
        if (it->second.empty())
            return false;
        return true;
    }

    const wchar_t* CCmdLineParser::GetVal(const wchar_t* sKey) const
    {
        const auto it = findKey(sKey);
        if (it == m_valueMap.cend())
            return nullptr;
        return it->second.c_str();
    }

    std::wstring CCmdLineParser::GetValStr(const wchar_t* sKey) const
    {
        const auto it = findKey(sKey);
        if (it == m_valueMap.cend())
            return {};
        return it->second;
    }

    long CCmdLineParser::GetLongVal(const wchar_t* sKey) const
    {
        const auto it = findKey(sKey);
        if (it == m_valueMap.cend())
            return 0;
        // wcstol, not std::stol: the contract (_wtol's) is "leading digits, else 0".
        // std::stol throws on an empty or non-numeric value, so a bare /line: on the
        // command line terminated the process.
        return std::wcstol(it->second.c_str(), nullptr, 10);
    }

    __int64 CCmdLineParser::GetLongLongVal(const wchar_t* sKey) const
    {
        const auto it = findKey(sKey);
        if (it == m_valueMap.cend())
            return 0;
        return std::wcstoll(it->second.c_str(), nullptr, 10);   // as GetLongVal: no throw
    }

    CCmdLineParser::ITERPOS CCmdLineParser::begin() const
    {
        return m_valueMap.cbegin();
    }

    CCmdLineParser::ITERPOS CCmdLineParser::getNext(ITERPOS& pos, std::wstring& sKey, std::wstring& sValue) const
    {
        if (m_valueMap.cend() == pos)
        {
            sKey.clear();
            return pos;
        }
        else
        {
            sKey = pos->first;
            sValue = pos->second;
            return ++pos;
        }
    }

    bool CCmdLineParser::isLast(const ITERPOS& pos) const
    {
        return (pos == m_valueMap.cend());
    }

    std::wstring CCmdLineParser::EscapeValue(const std::wstring_view input)
    {
        std::wstring result;
        result += L'"';

        result.reserve(input.size());
        for (const wchar_t c : input)
        {
            if (c == L'"')
                result += L'"';

            result += c;
        }
        result += L'"';

        return result;
    }
}

export using TCmdLineParser::CCmdLineParser;