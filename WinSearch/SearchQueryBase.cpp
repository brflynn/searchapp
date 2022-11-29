#include "pch.h"
#include "SearchQueryHelper.h"
#include <intsafe.h>
#include <NTQuery.h>
#include <propkey.h>
#include <SearchResult.h>
#include "Logging.h"

using namespace winrt::Windows::Foundation::Collections;

void SearchQueryBase::FetchRows(_Out_ ULONGLONG* totalFetched)
{
    ULONGLONG fetched = 0;
    *totalFetched = 0;

    winrt::com_ptr<IGetRow> getRow = m_rowset.as<IGetRow>();

    DBCOUNTITEM rowCountReturned;

    do
    {
        HROW rowBuffer[5000] = {}; // Request enough large batch to increase efficiency
        HROW* rowReturned = rowBuffer;

        THROW_IF_FAILED(m_rowset->GetNextRows(DB_NULL_HCHAPTER, 0, ARRAYSIZE(rowBuffer), &rowCountReturned, &rowReturned));
        THROW_IF_FAILED(ULongLongAdd(fetched, rowCountReturned, &fetched));

        for (unsigned int i = 0; (i < rowCountReturned); i++)
        {
            winrt::com_ptr<IUnknown> unknown;
            THROW_IF_FAILED(getRow->GetRowFromHROW(nullptr, rowBuffer[i], IID_IPropertyStore, unknown.put()));
            winrt::com_ptr<IPropertyStore> propStore = unknown.as<IPropertyStore>();

            m_numResults++;
            OnFetchRowCallback(propStore.get());
        }

        THROW_IF_FAILED(m_rowset->ReleaseRows(rowCountReturned, rowReturned, nullptr, nullptr, nullptr));
    } while (rowCountReturned > 0);

    *totalFetched = fetched;
}

DWORD SearchQueryBase::GetReuseWhereId(IRowset* rowset)
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

void SearchQueryBase::PrimeIndexAndCacheWhereId()
{
    // We need to generate a search query string with the search text the user entered above
    std::wstring queryStr = GetPrimingQueryString();

    winrt::com_ptr<ICommandText> cmdTxt;
    GetCommandText(cmdTxt);
    THROW_IF_FAILED(cmdTxt->SetCommandText(DBGUID_DEFAULT, queryStr.c_str()));

    DBROWCOUNT rowCount = 0;
    winrt::com_ptr<IUnknown> unkRowsetPtr;
    THROW_IF_FAILED(cmdTxt->Execute(nullptr, IID_IRowset, nullptr, &rowCount, unkRowsetPtr.put()));

    m_reuseRowset = unkRowsetPtr.as<IRowset>();

    m_reuseWhereID = GetReuseWhereId(m_reuseRowset.get());
}

void SearchQueryBase::ExecuteQueryStringSync(PCWSTR queryStr)
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

        winrt::com_ptr<ICommandText> cmdTxt;
        GetCommandText(cmdTxt);
        THROW_IF_FAILED(cmdTxt->SetCommandText(DBGUID_DEFAULT, queryStr));

        DBROWCOUNT rowCount = 0;
        winrt::com_ptr<IUnknown> unkRowsetPtr;
        THROW_IF_FAILED(cmdTxt->Execute(nullptr, IID_IRowset, nullptr, &rowCount, unkRowsetPtr.put()));

        m_rowset = unkRowsetPtr.as<IRowset>();

        OnPreFetchRows();

        ULONGLONG rowsFetched = 0;
        FetchRows(&rowsFetched);
    }
    CATCH_LOG();

    OnPostFetchRows();
}

void SearchQueryBase::GetCommandText(winrt::com_ptr<ICommandText>& cmdText)
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