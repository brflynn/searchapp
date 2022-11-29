#pragma once

#include "pch.h"
#include "Logging.h"
#include "SearchResult.h"
#include "SearchResultHelpers.h"

struct __declspec(uuid("7f8e1286-559c-4da1-b4dc-1b414d0da123")) ISearchQuery : ::IUnknown
{
    virtual void Init() = 0;
    virtual void ExecuteSync() = 0;
    virtual PCWSTR GetQueryString() = 0;
    virtual DWORD GetNumResults() = 0;
};

struct __declspec(uuid("7f0c248a-4dc7-45f7-8ea1-9be6545eee8e")) ISearchUXQuery : ::IUnknown
{
    virtual void Init(bool contentSearchEnabled, bool mailSearchEnabled, bool allUsersSearchEnabled) = 0;
    virtual void Execute(PCWSTR searchText, DWORD cookie) = 0;
    virtual DWORD GetCookie() = 0;
    virtual bool GetContentSearchEnabled() = 0;
    virtual winrt::WinSearch::SearchResult GetResult(DWORD idx) = 0;
    virtual void WaitForQueryCompletedEvent() = 0;
    virtual void CancelOutstandingQueries() = 0;
};

struct SearchQueryBase
{
protected:
    SearchQueryBase() {};

    void FetchRows(_Out_ ULONGLONG* totalFetched);
    void ExecuteQueryStringSync(PCWSTR queryStr);
    void PrimeIndexAndCacheWhereId();
    void GetCommandText(winrt::com_ptr<ICommandText>& cmdText);
    DWORD GetReuseWhereId(IRowset* rowset);

    virtual void OnPreFetchRows() = 0;
    virtual void OnFetchRowCallback(IPropertyStore* propStore) = 0;
    virtual void OnPostFetchRows() = 0;
    virtual std::wstring GetPrimingQueryString() = 0;

    winrt::com_ptr<IRowset> m_rowset;
    winrt::com_ptr<IRowset> m_reuseRowset;
    wil::critical_section m_cs;

    DWORD m_reuseWhereID{0};
    DWORD m_numResults{0};
};

__declspec(selectany) CLSID CLSID_CollatorDataSource = { 0x9E175B8B, 0xF52A, 0x11D8, 0xB9, 0xA5, 0x50, 0x50, 0x54, 0x50, 0x30, 0x30 };

union FILETIME64
{
    INT64 quad;
    FILETIME ft;
};

winrt::com_ptr<ISearchQuery> CreateSearchQueryHelper();
winrt::com_ptr<ISearchQuery> CreateStaticPropertyAnalysisQuery();

struct QueryStringBuilder
{
    const PCWSTR c_select = L"SELECT";
    const PCWSTR c_fromIndex = L"FROM SystemIndex WHERE";
    const PCWSTR c_scopeFileConditions = L" SCOPE='file:' AND SCOPE <> 'file://C:/users/tltay'";
    const PCWSTR c_scopeEmailConditions = L" OR SCOPE='mapi:' OR SCOPE='mapi16:'";
    const PCWSTR c_orderConditions = L" ORDER BY System.Search.Rank, System.DateModified, System.ItemNameDisplay DESC";
    std::wstring m_usersScope;
    std::vector<std::wstring> m_properties{ L"System.ItemUrl", L"System.ItemNameDisplay", L"path", L"System.Search.EntryID",
        L"System.Kind", L"System.KindText", L"System.Search.GatherTime", L"System.Search.QueryPropertyHits" };
    std::wstring m_scopeStr; // can be overriden to provide a custom scope


    std::wstring GenerateSingleUserScope()
    {
        wchar_t myProfileDir[MAX_PATH]{};
        GetEnvironmentVariable(L"%USERPROFILE%", myProfileDir, ARRAYSIZE(myProfileDir));
        std::wstring myProfileDirStr(myProfileDir);

        // find the trailing slash
//        _tracelog(L"CurrentUserProfilePath: %s\n", userProfilesDir.c_str());

        // Get all the users, and filter out the one that is not us
        std::wstring scopeStr;
        auto users = winrt::Windows::System::User::FindAllAsync().get();
        for (const auto& user : users)
        {
            std::wstring foundUserProfileDir(winrt::Windows::Storage::UserDataPaths::GetForUser(user).Profile());
            _tracelog(L"Found user profile: %s\n", foundUserProfileDir.c_str());
            scopeStr += L" AND SCOPE <> ";
            scopeStr += foundUserProfileDir.c_str();
        }

        return scopeStr;
    }

    void SetScope(PCWSTR scope)
    {
        m_scopeStr = scope;
    }

    void SetProperties(std::vector<std::wstring> const& properties)
    {
        m_properties = properties;
    }

    std::wstring GenerateProperties()
    {
        std::wstring propertyStr;
        for (auto prop : m_properties)
        {
            propertyStr += L' ';
            propertyStr += prop.c_str();
            propertyStr += L',';
        }
        // remove the last comma
        propertyStr.pop_back();

        // add a space
        propertyStr += L' ';

        return propertyStr;
    }

    std::wstring GenerateSelectQueryWithScope(bool mailSearchEnabled, bool allUsersSearchEnabled)
    {
        if (!allUsersSearchEnabled && m_usersScope.empty())
        {
            m_usersScope = GenerateSingleUserScope();
        }

        std::wstring queryStr(c_select);
        queryStr += GenerateProperties();
        queryStr += c_fromIndex;
        queryStr += L' ';
        queryStr += L"(";

        if (m_scopeStr.empty())
        {
            queryStr += c_scopeFileConditions;
        }
        else
        {
            queryStr += m_scopeStr.c_str();
        }
        
        
        if (mailSearchEnabled)
        {
            queryStr += c_scopeEmailConditions;
        }

        if (!allUsersSearchEnabled)
        {
            //queryStr += m_usersScope;
        }

        queryStr += L")";
        return queryStr;
    }

    std::wstring GeneratePrimingQuery(bool mailSearchEnabled, bool allUsersSearchEnabled)
    {
        std::wstring queryStr(GenerateSelectQueryWithScope(mailSearchEnabled, allUsersSearchEnabled));
        queryStr += c_orderConditions;

        _tracelog(L"\nPriming SQL: %ws", queryStr.c_str());
        return queryStr;
    }

    std::vector<std::wstring> GenerateSearchQueryTokens(PCWSTR searchText)
    {
        std::vector<std::wstring> strings;
        std::wstringstream f(searchText);
        std::wstring s;
        while (std::getline(f, s, L' ')) {
            strings.push_back(s);
        }
        return strings;
    }

    std::wstring GenerateQuery(PCWSTR searchText, bool contentSearchEnabled, bool mailSearchEnabled, bool allUsersSearchEnabled, DWORD whereId)
    {
        std::wstring queryStr(GenerateSelectQueryWithScope(mailSearchEnabled, allUsersSearchEnabled));
        size_t lenSearchText = wcslen(searchText);

        // Filter by item name display only
        if ((lenSearchText > 0))
        {
            queryStr += L" AND (CONTAINS(System.ItemNameDisplay, '\"";
            queryStr += searchText;
            queryStr += L"*\"')";
        }

        std::vector<std::wstring> tokens = GenerateSearchQueryTokens(searchText);

        // Are we searching contents?
        if (contentSearchEnabled && (lenSearchText > 0))
        {
            queryStr += L" OR (";
            for (int i = 0; i < tokens.size(); ++i)
            {
                queryStr += L"CONTAINS(*, '\"";
                queryStr += tokens[i].c_str();
                queryStr += L"*\"')";

                if (i < (tokens.size() - 1))
                {
                    queryStr += L" AND ";
                }
            }
            queryStr += L')';
        }

        // group the contains
        if (lenSearchText)
        {
            queryStr += L")";
        }

        // Always add reuse where to the query
        queryStr += L" AND ReuseWhere(";
        queryStr += std::to_wstring(whereId);
        queryStr += L")";

        queryStr += c_orderConditions;

        _tracelog(L"SQL: %ws", queryStr.c_str());
        return queryStr;
    }

};

struct SmartPropVariant
{
    SmartPropVariant() { PropVariantInit(&m_pv); }
    ~SmartPropVariant() { PropVariantClear(&m_pv); }

    std::wstring GetString()
    {
        THROW_HR_IF(E_INVALIDARG, (m_pv.vt != VT_LPWSTR));

        std::wstring prop = m_pv.pwszVal;
        return prop;
    }

    DWORD GetDWORD()
    {
        THROW_HR_IF(E_INVALIDARG, (m_pv.vt != VT_I4));

        return m_pv.lVal;
    }

    PROPVARIANT* put()
    {
        return &m_pv;
    }

    PROPVARIANT get()
    {
        return m_pv;
    }

    bool IsEmpty()
    {
        return ((m_pv.vt == VT_EMPTY) || (m_pv.vt == VT_NULL));
    }

private:
    PROPVARIANT m_pv;
};