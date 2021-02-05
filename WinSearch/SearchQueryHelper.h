#pragma once

#include "pch.h"
#include <string>
#include <sstream>

#include "SearchResult.h"
#include "SearchResultHelpers.h"

struct __declspec(uuid("7f8e1286-559c-4da1-b4dc-1b414d0da123")) ISearchQuery : ::IUnknown
{
    virtual void Init(bool async, bool contentSearchEnabled, bool mailSearchEnabled) = 0;
    virtual void Execute(PCWSTR searchText, DWORD cookie) = 0;
    virtual PCWSTR GetQueryString() = 0;
    virtual DWORD GetCookie() = 0;
    virtual bool GetContentSearchEnabled() = 0;
    virtual DWORD GetNumResults() = 0;
    virtual winrt::WinSearch::SearchResult GetResult(DWORD idx) = 0;
};

winrt::com_ptr<ISearchQuery> CreateSearchQueryHelper();

struct QueryStringBuilder
{
    const PCWSTR c_primeQuery = L"SELECT TOP 30 System.ItemUrl, System.ItemNameDisplay, path, System.Search.EntryID, System.Kind, System.KindText, System.Search.GatherTime FROM SystemIndex WHERE";
    const PCWSTR c_scopeFileConditions = L" SCOPE='file:'";
    const PCWSTR c_scopeEmailConditions = L" OR SCOPE='mapi:' OR SCOPE='mapi16:'";
    const PCWSTR c_orderConditions = L" ORDER BY System.DateModified, System.ItemNameDisplay DESC";

    std::wstring GenerateSelectQueryWithScope(bool mailSearchEnabled)
    {
        std::wstring queryStr(c_primeQuery);
        queryStr += L"(";
        queryStr += c_scopeFileConditions;
        if (mailSearchEnabled)
        {
            queryStr += c_scopeEmailConditions;
        }
        queryStr += L")";
        return queryStr;
    }

    std::wstring GeneratePrimingQuery(bool mailSearchEnabled)
    {
        std::wstring queryStr(GenerateSelectQueryWithScope(mailSearchEnabled));
        queryStr += c_orderConditions;

        return queryStr;
    }

    std::wstring GenerateQuery(PCWSTR searchText, bool contentSearchEnabled, bool mailSearchEnabled, DWORD whereId)
    {
        std::wstring queryStr(GenerateSelectQueryWithScope(mailSearchEnabled));
        size_t lenSearchText = wcslen(searchText);

        // Filter by item name display only
        if (lenSearchText > 0)
        {
            queryStr += L" AND (CONTAINS(System.ItemNameDisplay, '\"";
            queryStr += searchText;
            queryStr += L"*\"')";
        }

        // Are we searching contents?
        if (contentSearchEnabled && (lenSearchText > 0))
        {
            queryStr += L" OR CONTAINS(*, '\"";
            queryStr += searchText;
            queryStr += L"*\"')";
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

    bool IsEmpty()
    {
        return ((m_pv.vt == VT_EMPTY) || (m_pv.vt == VT_NULL));
    }

private:
    PROPVARIANT m_pv;
};