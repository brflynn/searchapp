#include "pch.h"
#include "SearchQueryHelper.h"
#include <intsafe.h>
#include <NTQuery.h>
#include <propkey.h>
#include <SearchResult.h>
#include "Logging.h"

using namespace winrt::Windows::Foundation::Collections;

struct SearchUXQueryHelper : winrt::implements<SearchUXQueryHelper, ISearchQuery, ISearchUXQuery>, public SearchQueryBase
{
public:
    // ISearchUXQuery
    void Init(bool contentSearchEnabled, bool mailSearchEnabled, bool allUsersSearchEnabled);
    bool GetContentSearchEnabled() { return m_contentSearchEnabled; }
    winrt::WinSearch::SearchResult GetResult(DWORD idx);
    void WaitForQueryCompletedEvent();
    void CancelOutstandingQueries();
    void Execute(PCWSTR searchText, DWORD cookie);
    DWORD GetCookie();

    // ISearchQuery
    void Init() {};
    void ExecuteSync() {};
    PCWSTR GetQueryString() { return m_searchText.c_str(); }
    DWORD GetNumResults() { return m_numResults; }

    // Other public methods
    static void CALLBACK QueryTimerCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER);
    void OnPreFetchRows() override;
    void OnPostFetchRows() override;
    void OnFetchRowCallback(IPropertyStore* propStore) override;
    std::wstring GetPrimingQueryString() override;

private:
    void ExecuteSyncInternal();
    void CreateSearchResult(IPropertyStore* propStore);

    winrt::com_ptr<IRowset> m_rowset;
    winrt::com_ptr<IRowset> m_reuseRowset;
    wil::critical_section m_cs;

    DWORD m_cookie{};
    std::wstring m_searchText;
    bool m_contentSearchEnabled{};
    bool m_mailSearchEnabled{};
    bool m_allUsersSearchEnabled{};
    winrt::Windows::Foundation::Collections::IVector<winrt::WinSearch::SearchResult> m_searchResults;
    const DWORD m_queryTimerThreshold{ 85 };
    wil::unique_threadpool_timer m_queryTpTimer;
    wil::unique_event m_queryCompletedEvent;
};

winrt::com_ptr<ISearchQuery> CreateSearchQueryHelper()
{
    winrt::com_ptr<ISearchQuery> query{ winrt::make<SearchUXQueryHelper>().as<ISearchQuery>() };
    return query;
}

void SearchUXQueryHelper::QueryTimerCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER)
{
    SearchUXQueryHelper* pQueryHelper = reinterpret_cast<SearchUXQueryHelper*>(context);

    pQueryHelper->ExecuteSyncInternal();
}

void SearchUXQueryHelper::WaitForQueryCompletedEvent()
{
    ::WaitForSingleObject(m_queryCompletedEvent.get(), INFINITE);
}

void SearchUXQueryHelper::CreateSearchResult(IPropertyStore* propStore)
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
    auto searchResult = winrt::make<winrt::WinSearch::implementation::SearchResult>(
        itemNameDisplay.GetString().c_str(),
        itemUrl.GetString().c_str(),
        filePath.c_str(),
        isMail,
        isFolder);
    if (searchResult.CanDisplay())
    {
        m_searchResults.Append(searchResult);
    }
}

winrt::WinSearch::SearchResult SearchUXQueryHelper::GetResult(DWORD idx)
{
    return m_searchResults.GetAt(idx);
}

void SearchUXQueryHelper::OnPreFetchRows()
{
    // If we've gotten this far we have successful results...only now clear the result list and update it
    m_searchResults = winrt::single_threaded_observable_vector<winrt::WinSearch::SearchResult>();
}

void SearchUXQueryHelper::OnPostFetchRows()
{
    // We're done...
    m_numResults = m_searchResults.Size(); // num results is really how many we display
    m_queryCompletedEvent.SetEvent();
}

void SearchUXQueryHelper::OnFetchRowCallback(IPropertyStore* propStore)
{
    CreateSearchResult(propStore);
}

void SearchUXQueryHelper::ExecuteSyncInternal()
{
    try
    {
        QueryStringBuilder builder;
        std::wstring queryStr = builder.GenerateQuery(m_searchText.c_str(), m_contentSearchEnabled, m_mailSearchEnabled, m_allUsersSearchEnabled, m_reuseWhereID);
        ExecuteQueryStringSync(queryStr.c_str());
    }
    CATCH_LOG();
}

void SearchUXQueryHelper::CancelOutstandingQueries()
{
    // Are we currently doing work? If so, let's cancel
    {
        auto lock = m_cs.lock();
        SetThreadpoolTimer(m_queryTpTimer.get(), nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(m_queryTpTimer.get(), TRUE);
        m_queryTpTimer.reset(nullptr);
    }
}

void SearchUXQueryHelper::Execute(PCWSTR searchText, DWORD cookie)
{
    // We should try to coaelse queries here so that we aren't firing one for every specific key entered...
    // If a query comes in within our threshold let's kill the firing one and create a new one
    auto lock = m_cs.lock();

    if (m_queryTpTimer.get() != nullptr)
    {
        // We cancel the outstanding query callback and queue a new one every time
        SetThreadpoolTimer(m_queryTpTimer.get(), nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(m_queryTpTimer.get(), TRUE);
        m_searchText = searchText;
        m_cookie = cookie;

        // queue query
        FILETIME64 ft = { -static_cast<INT64>(m_queryTimerThreshold) * 10000 };
        FILETIME fireTime = ft.ft;
        _tracelog(L"Queue query: %d\n", m_cookie);
        SetThreadpoolTimer(m_queryTpTimer.get(), &fireTime, 0, 0);
    }
}

std::wstring SearchUXQueryHelper::GetPrimingQueryString()
{
    QueryStringBuilder builder;
    std::wstring queryStr = builder.GeneratePrimingQuery(m_mailSearchEnabled, m_allUsersSearchEnabled);
    return queryStr;
}

void SearchUXQueryHelper::Init(bool contentSearchEnabled, bool mailSearchEnabled, bool allUsersSearchEnabled)
{
    // Create all the objects we will want cached
    try
    {
        m_contentSearchEnabled = contentSearchEnabled;
        m_mailSearchEnabled = mailSearchEnabled;
        m_allUsersSearchEnabled = allUsersSearchEnabled;
        m_queryTpTimer.reset(CreateThreadpoolTimer(SearchUXQueryHelper::QueryTimerCallback, reinterpret_cast<void*>(this), nullptr));
        THROW_LAST_ERROR_IF_NULL(m_queryTpTimer.get());

        m_queryCompletedEvent.create();
        THROW_LAST_ERROR_IF_NULL(m_queryCompletedEvent.get());

        // Execute a synchronous query on file/mapi items to prime the index and keep that handle around
        PrimeIndexAndCacheWhereId();
    }
    CATCH_LOG();
}

DWORD SearchUXQueryHelper::GetCookie()
{
    return m_cookie;
}