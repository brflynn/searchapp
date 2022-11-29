#include "pch.h"
#include "SearchQueryHelper.h"
#include <intsafe.h>
#include <NTQuery.h>
#include <propkey.h>
#include <SearchResult.h>
#include "Logging.h"

using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Storage;

struct StaticPropertyAnalysisQuery : winrt::implements<StaticPropertyAnalysisQuery, ISearchQuery>, public SearchQueryBase
{
public:
    // ISearchQuery
    void Init();
    void ExecuteSync();
    PCWSTR GetQueryString();
    DWORD GetNumResults();

    // Must implement
    void OnPreFetchRows() override {};
    void OnFetchRowCallback(IPropertyStore* propStore) override;
    void OnPostFetchRows() override {};
    std::wstring GetPrimingQueryString() override; 

private:
    std::vector<std::wstring> GetPropertyVector();
    void ExecuteQueriesSync();
    StorageFile GetAnalysisFile();
    HRESULT ExecuteSingleQuerySync(std::vector<std::wstring>& propVec, bool fetchRows);
    void ProcessItemInternal(IPropertyStore* propStore);
    void AddNewItemId(DWORD itemId);

    struct FileTypeInfo
    {
        DWORD totalPropertyCount;
        DWORD items;
    };
    std::vector<std::wstring> m_propertyVec;
    std::vector<std::wstring> m_invertedProperties;
    std::vector<std::wstring> m_columnProperties;
    std::vector<DWORD> m_items;
    std::unordered_map<std::wstring, FileTypeInfo> m_typePropertiesMap;
    DWORD m_totalValidProperties{ 0 };
    std::wstring m_analysisFileName;
    wil::critical_section m_cs;
    std::vector<IPropertyStore*> m_callbackStores;
    DWORD m_processing{ 0 };

    const PCWSTR c_scopePropertyAnalysisConditions = L" SCOPE='file:C:/users/brend/ExampleFiles'";
};

winrt::com_ptr<ISearchQuery> CreateStaticPropertyAnalysisQuery()
{
    winrt::com_ptr<ISearchQuery> query{ winrt::make<StaticPropertyAnalysisQuery>().as<ISearchQuery>() };
    return query;
}

std::vector<std::wstring> StaticPropertyAnalysisQuery::GetPropertyVector()
{
    return m_propertyVec;
}

std::wstring StaticPropertyAnalysisQuery::GetPrimingQueryString()
{
    return L"";
}

StorageFile StaticPropertyAnalysisQuery::GetAnalysisFile()
{
    StorageFolder storageFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
    StorageFile storageFile = storageFolder.CreateFileAsync(m_analysisFileName.c_str(), CreationCollisionOption::OpenIfExists).get();
    return storageFile;
}

void StaticPropertyAnalysisQuery::AddNewItemId(DWORD itemId)
{
    for (DWORD i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i] == itemId)
        {
            // already added, bail out
            return;
        }
    }
    m_items.push_back(itemId);
}

void StaticPropertyAnalysisQuery::ProcessItemInternal(IPropertyStore* propStore)
{
    try
    {
        DWORD propCount{ 0 };
        THROW_IF_FAILED(propStore->GetCount(&propCount));
        m_totalValidProperties += propCount;

        for (DWORD i = 0; i < propCount; ++i)
        {
            PROPERTYKEY key{};
            THROW_IF_FAILED(propStore->GetAt(i, &key));

            SmartPropVariant propVar;
            THROW_IF_FAILED(propStore->GetValue(key, propVar.put()));

            winrt::com_ptr<IPropertyDescription> propDesc;
            THROW_IF_FAILED(PSGetPropertyDescription(key, IID_PPV_ARGS(propDesc.put())));

            LPWSTR propName;
            THROW_IF_FAILED(propDesc->GetCanonicalName(&propName));
            std::wstring propData(propName);
            propData += L": ";

            if (propVar.IsEmpty())
            {
                //_tracelog(L"\n%s empty, moving on.", propName);
                continue;
            }

            // Avoid specific properties
            if (_wcsicmp(propName, L"System.ItemId") == 0)
            {
                AddNewItemId(propVar.GetDWORD());
            }

            PROPDESC_SEARCHINFO_FLAGS searchInfoFlags;
            THROW_IF_FAILED(propDesc.as<IPropertyDescriptionSearchInfo>()->GetSearchInfoFlags(&searchInfoFlags));
            if (WI_AreAllFlagsClear(searchInfoFlags, PDSIF_ININVERTEDINDEX | PDSIF_ISCOLUMN | PDSIF_ISCOLUMNSPARSE))
            {
                //_tracelog(L"%s is not inverted or a column. Moving on.", propName);
                continue;
            }

            wchar_t propVarStr[4096]{};
            if (FAILED(PropVariantToString(propVar.get(), propVarStr, ARRAYSIZE(propVarStr))))
            {
                //_tracelog(L"\nError converting %s to string.", propName);
                continue;
            }
            propData += propVarStr;

            // Output the property and the text
            FileIO::AppendTextAsync(GetAnalysisFile(), L"\n\n").get();
            FileIO::AppendTextAsync(GetAnalysisFile(), propName).get();
            FileIO::AppendTextAsync(GetAnalysisFile(), L": ").get();
            FileIO::AppendTextAsync(GetAnalysisFile(), propVarStr).get();

            if (key == PKEY_FileExtension)
            {
                // Great, add the # of properties for this type
                auto it = m_typePropertiesMap.find(std::wstring(propVarStr));
                if (it != m_typePropertiesMap.end())
                {
                    it->second.items++;
                    it->second.totalPropertyCount += propCount;
                }
                else
                {
                    FileTypeInfo info{ propCount, 1 };
                    m_typePropertiesMap.insert(std::pair(std::wstring(propVarStr), info));
                }
            }
        }
    }
    catch (...)
    {
        _tracelog(L"Exeception found in processing item.");
    }
}

void StaticPropertyAnalysisQuery::OnFetchRowCallback(IPropertyStore* propStore)
{
    ProcessItemInternal(propStore);
}

HRESULT StaticPropertyAnalysisQuery::ExecuteSingleQuerySync(std::vector<std::wstring>& propVec, bool fetchRows)
{
    if (fetchRows)
    {
        // if we're fetching the rows, always add System.Search.EntryID
        propVec.push_back(std::wstring(L"workid"));
    }

    QueryStringBuilder queryBuilder;
    queryBuilder.SetProperties(propVec);
    queryBuilder.SetScope(c_scopePropertyAnalysisConditions);
    std::wstring queryStr = queryBuilder.GeneratePrimingQuery(false, false);
    winrt::com_ptr<ICommandText> cmdTxt;
    GetCommandText(cmdTxt);
    RETURN_IF_FAILED(cmdTxt->SetCommandText(DBGUID_DEFAULT, queryStr.c_str()));

    DBROWCOUNT rowCount = 0;
    winrt::com_ptr<IUnknown> unkRowsetPtr;
    RETURN_IF_FAILED(cmdTxt->Execute(nullptr, IID_IRowset, nullptr, &rowCount, unkRowsetPtr.put()));

    if (fetchRows)
    {
        m_rowset = unkRowsetPtr.as<IRowset>();

        ULONGLONG rowsFetched = 0;
        FetchRows(&rowsFetched);
    }

    return S_OK;
}

void StaticPropertyAnalysisQuery::ExecuteQueriesSync()
{
    // Collect all the properties
    winrt::com_ptr<IPropertyDescriptionList> propList;
    THROW_IF_FAILED(PSEnumeratePropertyDescriptions(PDEF_ALL, IID_PPV_ARGS(propList.put())));

    UINT propCount{ 0 };
    THROW_IF_FAILED(propList->GetCount(&propCount));

    FileIO::AppendTextAsync(GetAnalysisFile(), L"\nTotal Properties: ").get();
    FileIO::AppendTextAsync(GetAnalysisFile(), std::to_wstring(propCount).c_str()).get();

    for (unsigned int i = 0; i < propCount; ++i)
    {
        winrt::com_ptr<IPropertyDescription> propDesc;
        THROW_IF_FAILED(propList->GetAt(i, IID_PPV_ARGS(propDesc.put())));

        LPWSTR propName;
        THROW_IF_FAILED(propDesc->GetCanonicalName(&propName));

        if (_wcsicmp(propName, L"System.Search.EntryID") == 0)
        {
            // skip the entry id as we always request that for the analysis query
            continue;
        }

        PROPDESC_SEARCHINFO_FLAGS searchInfoFlags;
        THROW_IF_FAILED(propDesc.as<IPropertyDescriptionSearchInfo>()->GetSearchInfoFlags(&searchInfoFlags));
        if (searchInfoFlags & PDSIF_ISCOLUMN)
        {
            _tracelog(L"\nColumn: %s", propName);
            m_propertyVec.push_back(std::wstring(propName));
            m_columnProperties.push_back(propName);
        }

        if (searchInfoFlags & PDSIF_ININVERTEDINDEX)
        {
            m_invertedProperties.push_back(propName);
        }
    }

    // Now, execute all queries throwing away properties that are not actually columns...
    // this is an indexer bug...but for analysis let's just toss them out
    std::vector<std::wstring> subPropVec;
    std::vector<std::wstring> swapPropVec;
    for (auto it : m_propertyVec)
    {
        subPropVec.push_back(it);

        if (_wcsicmp(it.c_str(), L"System.FileExtension") == 0)
        {
            _tracelog(L"");
        }

        if (FAILED(ExecuteSingleQuerySync(subPropVec, false)))
        {
            _tracelog(L"\n%s was an invalid property in the query. Removing.", subPropVec[0].c_str());
        }
        else
        {
            swapPropVec.push_back(it);
        }
        subPropVec.clear();
    }

    _tracelog(L"\nSwapping prop vec size: %d with %d", m_propertyVec.size(), swapPropVec.size());
    m_propertyVec.swap(swapPropVec);

    // 50 properties at a time
    std::vector<std::wstring> queryPropVec;
    for (DWORD i = 0; i < m_propertyVec.size(); ++i)
    {
        queryPropVec.push_back(m_propertyVec[i]);
        if (_wcsicmp(m_propertyVec[i].c_str(), L"System.FileExtension") == 0)
        {
            _tracelog(L"");
        }

        if (queryPropVec.size() == 50)
        {
            if (FAILED(ExecuteSingleQuerySync(queryPropVec, true)))
            {
                _tracelog(L"Query failed!!!");
            }
            queryPropVec.clear();
            //break;
        }
    }

    // Always issue one query at the end with the remaining
    if (queryPropVec.size() > 0)
    {
        if (FAILED(ExecuteSingleQuerySync(queryPropVec, true)))
        {
            _tracelog(L"Query failed!!!");
        }
        queryPropVec.clear();
    }
}

void StaticPropertyAnalysisQuery::ExecuteSync()
{
    try
    {
        // Loop through, executing our queries based on the properties
        // and dump out all of the relevant information into the cache before we write it out
        ExecuteQueriesSync();

        // Now, output all the property data to the text file
        // start with the average, followed by the inverted properties
        FileIO::AppendTextAsync(GetAnalysisFile(), L"\n\nAverage Properties Per File Type\n").get();

        m_numResults = m_items.size();
        for (auto it : m_typePropertiesMap)
        {
            float avgPropCount = static_cast<float>(it.second.totalPropertyCount / m_numResults);
            FileIO::AppendTextAsync(GetAnalysisFile(), it.first.c_str()).get();
            FileIO::AppendTextAsync(GetAnalysisFile(), L": ").get();
            FileIO::AppendTextAsync(GetAnalysisFile(), std::to_wstring(avgPropCount).c_str()).get();
            FileIO::AppendTextAsync(GetAnalysisFile(), L"\n\n").get();
        }

        FileIO::AppendTextAsync(GetAnalysisFile(), L"\n\nAverage Properties Per File: ").get();

        float avgPropertiesPerFile = static_cast<float>(m_totalValidProperties / m_numResults);
        FileIO::AppendTextAsync(GetAnalysisFile(), std::to_wstring(avgPropertiesPerFile).c_str()).get();

        // Output all inverted/column properties
        FileIO::AppendTextAsync(GetAnalysisFile(), L"\n\nInverted Properties").get();
        for (auto it : m_invertedProperties)
        {
            std::wstring prop(L"\n");
            prop += it;
            FileIO::AppendTextAsync(GetAnalysisFile(), prop.c_str()).get();
        }

        FileIO::AppendTextAsync(GetAnalysisFile(), L"\n\nColumnProperties").get();
        for (auto it : m_columnProperties)
        {
            std::wstring prop(L"\n");
            prop += it;
            FileIO::AppendTextAsync(GetAnalysisFile(), prop.c_str()).get();
        }
    }
    CATCH_LOG();
}

void StaticPropertyAnalysisQuery::Init()
{
    try
    {
        StorageFolder storageFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
        StorageFile storageFile = storageFolder.CreateFileAsync(L"StaticPropertyAnalysis.txt", CreationCollisionOption::GenerateUniqueName).get();
        m_analysisFileName = storageFile.Name();
    }
    CATCH_LOG();
}

PCWSTR StaticPropertyAnalysisQuery::GetQueryString()
{
    return L"";
}

DWORD StaticPropertyAnalysisQuery::GetNumResults()
{
    return m_numResults;
}