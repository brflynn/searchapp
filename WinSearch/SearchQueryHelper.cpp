#include "pch.h"
#include "SearchQueryHelper.h"
#include <intsafe.h>
#include <NTQuery.h>
#include <propkey.h>
#include <SearchResult.h>

using namespace winrt::Windows::Foundation::Collections;

CLSID CLSID_CollatorDataSource = { 0x9E175B8B, 0xF52A, 0x11D8, 0xB9, 0xA5, 0x50, 0x50, 0x54, 0x50, 0x30, 0x30 };

struct SearchQueryHelper : winrt::implements<SearchQueryHelper, ISearchQuery>
{
public:
    // ISearchQuery
    void Init(bool async, bool contentSearchEnabled, bool mailSearchEnabled);
    void Execute(PCWSTR searchText, DWORD cookie);
    DWORD GetCookie();
    PCWSTR GetQueryString();
    DWORD GetNumResults();
    bool GetContentSearchEnabled() { return m_contentSearchEnabled; }
    static void CALLBACK QueryCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK);
    winrt::WinSearch::SearchResult GetResult(DWORD idx);

private:
    void FetchRows(_Out_ ULONGLONG* totalFetched);
    void CancelOutstandingQueries();
    void ExecuteSync();
    void PrimeIndexAndCacheWhereId();
    void CreateSearchResult(int32_t idx, IPropertyStore* propStore);
    void GetCommandText(winrt::com_ptr<ICommandText> & cmdText);
    DWORD GetReuseWhereId(IRowset* rowset);

    winrt::com_ptr<IRowset> m_rowset;
    winrt::com_ptr<IRowset> m_reuseRowset;
    winrt::com_ptr<ISearchQueryHelper> m_queryHelper;
    wil::unique_threadpool_work m_tpWork;
    wil::critical_section m_cs;

    DWORD m_cookie = 0;
    bool m_async = false;
    std::wstring m_searchText;
    bool m_newQuery = false;
    bool m_contentSearchEnabled = false;
    bool m_mailSearchEnabled = false;
    DWORD m_reuseWhereID = 0;
    DWORD m_numResults = 0;

    winrt::Windows::Foundation::Collections::IVector<winrt::WinSearch::SearchResult> m_searchResults;
};

winrt::com_ptr<ISearchQuery> CreateSearchQueryHelper()
{
    winrt::com_ptr<ISearchQuery> query{ winrt::make<SearchQueryHelper>().as<ISearchQuery>() };
    return query;
}

void SearchQueryHelper::QueryCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK)
{
    SearchQueryHelper* pQueryHelper = reinterpret_cast<SearchQueryHelper*>(context);

    pQueryHelper->ExecuteSync();
}

void SearchQueryHelper::CreateSearchResult(int32_t idx, IPropertyStore* propStore)
{
    SmartPropVariant itemNameDisplay;
    THROW_IF_FAILED(propStore->GetValue(PKEY_ItemNameDisplay, itemNameDisplay.put()));

    SmartPropVariant itemUrl;
    THROW_IF_FAILED(propStore->GetValue(PKEY_ItemUrl, itemUrl.put()));

    // Need to calculate the image path to use, and let's try to do that based off of the kind
    SmartPropVariant kindText;
    THROW_IF_FAILED(propStore->GetValue(PKEY_KindText, kindText.put()));

    bool isFolder = false;
    if (!kindText.IsEmpty())
    {
        if (wcscmp(kindText.GetString().c_str(), L"Folder") == 0)
        {
            isFolder = true;
        }
    }

    std::wstring filePath(itemUrl.GetString());
    bool convertedToFilePath = false;
    ConvertUrlToFilePath(filePath, &convertedToFilePath);
    bool isMail = !convertedToFilePath;

    // Create the actual result object
    m_searchResults.InsertAt(idx, winrt::make<winrt::WinSearch::implementation::SearchResult>(
        itemNameDisplay.GetString().c_str(),
        itemUrl.GetString().c_str(),
        filePath.c_str(),
        isMail,
        isFolder));
}

winrt::WinSearch::SearchResult SearchQueryHelper::GetResult(DWORD idx)
{
    return m_searchResults.GetAt(idx);
}

void SearchQueryHelper::FetchRows(_Out_ ULONGLONG* totalFetched)
{
    ULONGLONG fetched = 0;
    *totalFetched = 0;

    winrt::com_ptr<IGetRow> getRow = m_rowset.as<IGetRow>();

    DBCOUNTITEM rowCountReturned;

    do
    {
        HROW rowBuffer[50] = {}; // Request enough large batch to increase efficiency
        HROW* rowReturned = rowBuffer;

        THROW_IF_FAILED(m_rowset->GetNextRows(DB_NULL_HCHAPTER, 0, ARRAYSIZE(rowBuffer), &rowCountReturned, &rowReturned));
        THROW_IF_FAILED(ULongLongAdd(fetched, rowCountReturned, &fetched));

        for (unsigned int i = 0; (i < rowCountReturned); i++)
        {
            winrt::com_ptr<IUnknown> unknown;
            THROW_IF_FAILED(getRow->GetRowFromHROW(nullptr, rowBuffer[i], IID_IPropertyStore, unknown.put()));
            winrt::com_ptr<IPropertyStore> propStore = unknown.as<IPropertyStore>();

            // Get the properties we asked for...and cache them in our result store
            CreateSearchResult(i, propStore.get());
        }

        THROW_IF_FAILED(m_rowset->ReleaseRows(rowCountReturned, rowReturned, nullptr, nullptr, nullptr));
    } while (rowCountReturned > 0);

    *totalFetched = fetched;
}

DWORD SearchQueryHelper::GetReuseWhereId(IRowset* rowset)
{
    winrt::com_ptr<IRowsetInfo> rowsetInfo;
    THROW_IF_FAILED(rowset->QueryInterface(IID_PPV_ARGS(rowsetInfo.put())));

    DBPROPIDSET propset;
    DBPROPSET* prgPropSets;
    DBPROPID whereid = MSIDXSPROP_WHEREID;
    propset.rgPropertyIDs = &whereid;
    propset.cPropertyIDs = 1;

    propset.guidPropertySet = DBPROPSET_MSIDXS_ROWSETEXT;
    ULONG cPropertySets;

    THROW_IF_FAILED(rowsetInfo->GetProperties(1, &propset, &cPropertySets, &prgPropSets));

    wil::unique_cotaskmem_ptr<DBPROP> sprgProps(prgPropSets->rgProperties);
    wil::unique_cotaskmem_ptr<DBPROPSET> sprgPropSets(prgPropSets);

    return prgPropSets->rgProperties->vValue.ulVal;
}

void SearchQueryHelper::PrimeIndexAndCacheWhereId()
{
    // We need to generate a search query string with the search text the user entered above
    QueryStringBuilder builder;
    std::wstring queryStr = builder.GeneratePrimingQuery(m_mailSearchEnabled);

    winrt::com_ptr<ICommandText> cmdTxt;
    GetCommandText(cmdTxt);
    THROW_IF_FAILED(cmdTxt->SetCommandText(DBGUID_DEFAULT, queryStr.c_str()));

    DBROWCOUNT rowCount = 0;
    winrt::com_ptr<IUnknown> unkRowsetPtr;
    THROW_IF_FAILED(cmdTxt->Execute(nullptr, IID_IRowset, nullptr, &rowCount, unkRowsetPtr.put()));

    m_reuseRowset = unkRowsetPtr.as<IRowset>();

    m_reuseWhereID = GetReuseWhereId(m_reuseRowset.get());
}

void SearchQueryHelper::ExecuteSync()
{
    try
    {
        auto lock = m_cs.lock();

        // We need to generate a search query string with the search text the user entered above
        if (m_rowset != nullptr)
        {
            // We have a previous rowset, this means the user is typing and we should store this
            // recapture the where ID from this so the next ExecuteSync call will be faster
            m_reuseRowset = m_rowset;
            m_reuseWhereID = GetReuseWhereId(m_reuseRowset.get());
        }

        m_rowset = nullptr;

        QueryStringBuilder builder;
        std::wstring queryStr = builder.GenerateQuery(m_searchText.c_str(), m_contentSearchEnabled, m_mailSearchEnabled, m_reuseWhereID);

        winrt::com_ptr<ICommandText> cmdTxt;
        GetCommandText(cmdTxt);
        THROW_IF_FAILED(cmdTxt->SetCommandText(DBGUID_DEFAULT, queryStr.c_str()));

        DBROWCOUNT rowCount = 0;
        winrt::com_ptr<IUnknown> unkRowsetPtr;
        THROW_IF_FAILED(cmdTxt->Execute(nullptr, IID_IRowset, nullptr, &rowCount, unkRowsetPtr.put()));

        m_rowset = unkRowsetPtr.as<IRowset>();

        // If we've gotten this far we have successful results...only now clear the result list and update it
        m_searchResults = winrt::single_threaded_observable_vector<winrt::WinSearch::SearchResult>();

        ULONGLONG rowsFetched = 0;
        FetchRows(&rowsFetched);

        m_numResults = static_cast<DWORD>(rowsFetched);
    }
    CATCH_LOG();
}

void SearchQueryHelper::CancelOutstandingQueries()
{
    // Are we currently doing work? If so, let's cancel
    {
        auto lock = m_cs.lock();
        m_newQuery = true;
    }
}

void SearchQueryHelper::Execute(PCWSTR searchText, DWORD cookie)
{
    m_searchText = searchText;
    m_cookie = cookie;
    if (m_async)
    {
        ::SubmitThreadpoolWork(m_tpWork.get());
    }
    else
    {
        ExecuteSync();
    }

}

void SearchQueryHelper::GetCommandText(winrt::com_ptr<ICommandText> & cmdText)
{
    // Query CommandText
    winrt::com_ptr<IDBInitialize> dataSource;
    THROW_IF_FAILED(CoCreateInstance(CLSID_CollatorDataSource, 0, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dataSource.put())));
    THROW_IF_FAILED(dataSource->Initialize());

    winrt::com_ptr<IDBCreateSession> session = dataSource.as<IDBCreateSession>();
    winrt::com_ptr<IUnknown> unkSessionPtr;
    THROW_IF_FAILED(session->CreateSession(0, IID_IDBCreateCommand, unkSessionPtr.put()));

    winrt::com_ptr<IDBCreateCommand> createCommand = unkSessionPtr.as<IDBCreateCommand>();
    winrt::com_ptr<IUnknown> unkCmdPtr;
    THROW_IF_FAILED(createCommand->CreateCommand(0, IID_ICommandText, unkCmdPtr.put()));
    
    cmdText = unkCmdPtr.as<ICommandText>();
}

void SearchQueryHelper::Init(bool async, bool contentSearchEnabled, bool mailSearchEnabled)
{
    // Create all the objects we will want cached
    try
    {
        m_async = async;
        m_contentSearchEnabled = contentSearchEnabled;
        m_mailSearchEnabled = mailSearchEnabled;
        m_tpWork.reset(CreateThreadpoolWork(SearchQueryHelper::QueryCallback, reinterpret_cast<void*>(this), nullptr));
        THROW_LAST_ERROR_IF_NULL(m_tpWork.get());

        // Execute a synchronous query on file/mapi items to prime the index and keep that handle around
        PrimeIndexAndCacheWhereId();
    }
    CATCH_LOG();
}

DWORD SearchQueryHelper::GetCookie()
{
    return m_cookie;
}

PCWSTR SearchQueryHelper::GetQueryString()
{
    return m_searchText.c_str();
}

DWORD SearchQueryHelper::GetNumResults()
{
    return m_numResults;
}