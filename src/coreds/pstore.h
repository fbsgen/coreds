#pragma once

#include <functional>
#include <deque>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include "b64.h"

namespace coreds {

enum class FetchType
{
    NONE,
    NEWER,
    OLDER,
    UPDATE
};

enum class SelectionType
{
    NONE = 0,
    RESET = 1,
    
    SELECT = 2,
    RETAIN = 3,
    RESELECT = 4
};

enum class SelectionFlags : uint8_t
{
    NONE = 0,
    CLICKED = 1,
    REFRESH = 2,
    CLICKED_UPDATE = 4,
    FORCE = 8,

    MASK_FORCE_OR_UPDATE = FORCE | CLICKED_UPDATE
};

struct ParamRangeKey
{
    bool desc{ true };
    int limit{ 10 };
    const char* start_key;
    const char* parent_key{ nullptr };
    
    void stringifyTo(std::string& buf)
    {
        buf += R"({"1":)";
        if (desc)
            buf += "true";
        else
            buf += "false";
        
        buf += R"(,"2":)";
        buf += std::to_string(limit);
        
        if (start_key)
        {
            buf += R"(,"3":")";
            buf.append(start_key, 12);
            buf += '"';
        }
        
        if (parent_key)
        {
            buf += R"(,"4":")";
            buf.append(parent_key, 12);
            buf += '"';
        }
        
        buf += '}';
    }
};

struct Opts
{
    int pageSize{ 10 };
    int multiplier{ 3 };
    //bool multiplier_conditional{ false };
};

enum class EventType
{
    DESC,
    LOADING,
    VISIBLE
};

template <typename T, typename F>
struct PojoStore
{
private:
    const std::function<void()> $populate = std::bind(&PojoStore::populate, this);
    std::deque<T> list;
    //T* startObj{ nullptr };
    
    bool loading_{ false };
    bool desc_{ true };
    
    int page{ 0 };
    int page_count { 0 };
    int page_vcount { 0 };
    int selected_idx { -1 };
    T* selected { nullptr };
    
    int pageSize{ 10 };
    int multiplier { 3 };
    
    FetchType fetchType { FetchType::NONE };
    
    std::string key_buf;
public:
    std::string errmsg;
    
    std::function<void()> $toggleSort{
        std::bind(&PojoStore::toggleSort, this)
    };
    std::function<void()> $refresh{
        std::bind(&PojoStore::refresh, this)
    };
    std::function<void()> $gotoFirst{
        std::bind(&PojoStore::gotoFirst, this)
    };
    std::function<void()> $gotoLast{
        std::bind(&PojoStore::gotoLast, this)
    };
    std::function<void()> $prevOrLoad{
        std::bind(&PojoStore::prevOrLoad, this)
    };
    std::function<void()> $nextOrLoad{
        std::bind(&PojoStore::nextOrLoad, this)
    };
    
    std::function<const char*(const T& pojo)> $fnKey;
    std::function<const char*(const F* message)> $fnKeyFB;
    std::function<bool(ParamRangeKey prk)> $fnFetch;
    std::function<void(T& pojo, const F* message)> $fnUpdate;
    std::function<void(EventType type, bool on)> $fnEvent;
    std::function<void(int idx, T* pojo, int64_t ts)> $fnPopulate;
    std::function<void(std::function<void()> op)> $fnCall;
    
    PojoStore()
    {
        key_buf.reserve(13); // + 1 for the null char
    }
    T* front()
    {
        return list.empty() ? nullptr : &list.front();
    }
    T* back()
    {
        return list.empty() ? nullptr : &list.back();
    }
    bool empty()
    {
        return list.empty();
    }
    int size()
    {
        return list.size();
    }
    bool loading()
    {
        return loading_;
    }
    void loading(bool val)
    {
        loading_ = val;
        $fnEvent(EventType::LOADING, val);
    }
    bool isDesc()
    {
        return desc_;
    }
    int getPage()
    {
        return page;
    }
    int getPageSize()
    {
        return pageSize;
    }
    int getPageCount()
    {
        return page_count;
    }
    int getVisibleCount()
    {
        return page_vcount;
    }
    T* getSelected()
    {
        return selected;
    }
    int getSelectedIdx()
    {
        return selected_idx;
    }
    void init(Opts opts)
    {
        pageSize = opts.pageSize;
        multiplier = opts.multiplier;
    }
    bool isVisible(int idx)
    {
        int pageStart = page * pageSize;
        return pageStart == idx || (idx > pageStart && idx < (pageStart + pageSize));
    }
    bool isPageToFirstDisabled()
    {
        return loading_ || 0 == page;
    }
    bool isPageToLastDisabled()
    {
        return loading_ || 0 == list.size() || page_count == page;
    }
    bool toggleDesc(std::function<void()> beforePopulate = nullptr)
    {
        if (loading_)
            return false;
        
        desc_ = !desc_;
        
        if (beforePopulate == nullptr)
        {
            $fnCall($populate);
        }
        else
        {
            beforePopulate();
            populate();
        }
        
        $fnEvent(EventType::DESC, desc_);
        
        return true;
    }
    void repaint()
    {
        $fnCall($populate);
    }
    void populate()
    {
        T* pojo;
        int size = list.size(),
            i = 0,
            selected_idx = -1,
            offset,
            populatePages,
            len,
            start;
        
        if (size == 0)
        {
            if (page != 0)
                page = 0;
            
            if (!desc_)
            {
                desc_ = true;
                $fnEvent(EventType::VISIBLE, desc_);
            }
        }
        else if (page != 0 && (page * pageSize) >= size)
        {
            page = 0;
        }
        
        populatePages = page * pageSize;
        len = std::min(pageSize, size - populatePages);
        start = desc_ ? populatePages : -populatePages;
        
        page_vcount = len;
        page_count = (size - 1) / pageSize;
        
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        
        // reset
        this->selected_idx = selected_idx;
        for (; i < len; i++)
        {
            offset = desc_ ? start + i : start + size - i - 1;
            pojo = &list[offset];
            
            if (selected_idx == -1 && selected && selected == pojo)
                this->selected_idx = selected_idx = i;
            
            $fnPopulate(i, pojo, now);
        }
        
        for (; i < pageSize; i++) $fnPopulate(i, nullptr, now);
    }
    /*
     * Returns true if an item was removed.
     */
    bool update(const flatbuffers::Vector<flatbuffers::Offset<F>>* updateList)
    {
        int populatePages, populateLen, idx, i, removed;
        int size = list.size();
        int updateLen = updateList == nullptr ? 0 : updateList->size();
        if (updateLen == 0)
        {
            if (size <= pageSize)
            {
                list.clear();
                $fnEvent(EventType::VISIBLE, false);
                return true;
            }
            
            populatePages = page * pageSize;
            if (desc_)
                list.erase(list.begin() + populatePages, list.begin() + (populatePages + size));
            else
                list.erase(list.begin(), list.begin() + (size - populatePages));
            
            $fnCall($populate);
            return true;
        }
        
        populateLen = pageSize;
        i = 0;
        removed = 0;
        populatePages = page * populateLen;
        idx = desc_ ? populatePages : size - populatePages - 1;
        
        for (;;)
        {
            auto update = updateList->Get(i);
            auto& existing = list[idx];
            if (0 == memcmp($fnKeyFB(update), $fnKey(existing), 12))
            {
                i++;
                
                $fnUpdate(existing, update);
                
                if (desc_)
                {
                    if (++idx == size)
                        break;
                }
                else if (--idx == -1)
                {
                    break;
                }
                
                if (i != updateLen)
                    continue;
                
                if (updateLen == populateLen)
                    break;
                
                list.erase(list.begin() + idx);
                removed++;
                size--;
                
                break;
            }
            
            list.erase(list.begin() + idx);
            removed++;
            size--;
            
            if (desc_)
            {
                if (idx == size)
                    break;
            }
            else if (--idx == -1)
            {
                break;
            }
        }
        
        for (; i < updateLen; i++)
        {
            if (desc_)
                list.emplace_back(updateList->Get(i));
            else
                list.emplace_front(updateList->Get(i));
        }
        
        // TODO check if current page is affected before you populate
        $fnCall($populate);
        return removed != 0;
    }
    void prependAll(const flatbuffers::Vector<flatbuffers::Offset<F>>* p, bool reversed = false)
    {
        if (reversed)
        {
            for (int i = p->size(); i--> 0;)
                list.emplace_front(p->Get(i));
        }
        else
        {
            for (int i = 0, len = p->size(); i < len; i++)
                list.emplace_front(p->Get(i));
        }
        
        // TODO check if current page is affected before you populate
        $fnCall($populate);
    }
    void appendAll(const flatbuffers::Vector<flatbuffers::Offset<F>>* p, bool reversed = false)
    {
        if (reversed)
        {
            for (int i = p->size(); i--> 0;)
                list.emplace_back(p->Get(i));
        }
        else
        {
            for (int i = 0, len = p->size(); i < len; i++)
                list.emplace_back(p->Get(i));
        }
        
        // TODO check if current page is affected before you populate
        $fnCall($populate);
    }
    bool cbFetchFailed() {
        if (fetchType == FetchType::NONE)
            return false;
        
        fetchType = FetchType::NONE;
        loading_ = false;
        $fnEvent(EventType::LOADING, false);
        return true;
    }
    bool cbFetchSuccess(const flatbuffers::Vector<flatbuffers::Offset<F>>* p) {
        if (fetchType == FetchType::NONE)
            return false;
        
        switch (fetchType)
        {
            case FetchType::NEWER:
                if (p == nullptr || 0 == p->size())
                    break;

                if (!list.empty())
                    prependAll(p, true);
                else
                    appendAll(p);
              break;
            case FetchType::OLDER:
                if (p == nullptr || 0 == p->size())
                    break;
                
                appendAll(p);
                break;
            case FetchType::UPDATE:
                update(p);
                break;
        }
        
        fetchType = FetchType::NONE;
        loading_ = false;
        $fnEvent(EventType::LOADING, false);
        return true;
    }
    bool fetchNewer()
    {
        if (loading_)
            return false;
        
        bool empty = list.empty();
        if (empty && !desc_)
        {
            desc_ = true;
            $fnEvent(EventType::DESC, true);
        }
        
        ParamRangeKey prk;
        prk.desc = empty;
        prk.limit = empty ? pageSize * multiplier + 1 : (desc_ ? pageSize : pageSize * multiplier);
        prk.start_key = empty ? nullptr : $fnKey(list.front());
        
        if (!$fnFetch(prk))
            return false;
        
        fetchType = FetchType::NEWER;
        loading_ = true;
        $fnEvent(EventType::LOADING, true);
        return true;
    }
    bool fetchOlder()
    {
        if (loading_ || list.empty())
            return false;
        
        ParamRangeKey prk;
        prk.desc = true;
        prk.limit = desc_ ? pageSize * multiplier : pageSize;
        //prk.start_key = $fnKey(desc_ ? list.back() : list.front());
        prk.start_key = $fnKey(list.back());
        
        if (!$fnFetch(prk))
            return false;
        
        fetchType = FetchType::OLDER;
        loading_ = true;
        $fnEvent(EventType::LOADING, true);
        return true;
    }
    bool fetch(bool newer)
    {
        return newer ? fetchNewer() : fetchOlder();
    }
    bool fetchUpdate()
    {
        if (list.empty())
            return fetchNewer();
        
        if (loading_)
            return false;
        
        int idx = page * pageSize;
        if (!desc_)
            idx = list.size() - idx - 1;
        
        // the first item in the visible list
        auto& pojo = list[idx];
        
        if (desc_)
            coreds::b64::incAndWriteKeyTo(key_buf, $fnKey(pojo));
        else
            coreds::b64::decAndWriteKeyTo(key_buf, $fnKey(pojo));
        
        ParamRangeKey prk;
        prk.desc = desc_;
        prk.limit = std::min(pageSize, static_cast<int>(list.size()));
        prk.start_key = key_buf.c_str();
        
        if (!$fnFetch(prk))
            return false;
        
        fetchType = FetchType::UPDATE;
        loading_ = true;
        $fnEvent(EventType::LOADING, true);
        return true;
    }
    bool appendPageInfoTo(std::string& buf)
    {
        int start, end;
        int size = list.size();
        if (size == 0)
        {
            // noop
        }
        else if (size == (start = (page * pageSize) + 1))
        {
            // 1 of 1
            const auto str = std::to_string(size);
            buf += str;
            buf += " of ";
            buf += str;
        }
        else if (size > (end = start + pageSize - 1))
        {
            buf += std::to_string(start);
            buf += " - ";
            buf += std::to_string(end);
            buf += " of ";
            buf += std::to_string(size);
        }
        else
        {
            const auto str = std::to_string(size);
            buf += std::to_string(start);
            buf += " - ";
            buf += str;
            buf += " of ";
            buf += str;
        }
        
        return size != 0;
    }
private:
    bool paginate(int idx, std::function<void()> beforePopulate)
    {
        page = idx;
        if (beforePopulate == nullptr)
        {
            $fnCall($populate);
        }
        else
        {
            beforePopulate();
            populate();
        }
        return true;
    }
public:
    bool pageTo(int idx, std::function<void()> beforePopulate = nullptr)
    {
        return !loading_ && idx != page && idx >= 0 && idx <= page_count && paginate(idx, beforePopulate);
    }
    bool pageToFirst(std::function<void()> beforePopulate = nullptr)
    {
        return !isPageToFirstDisabled() && paginate(0, beforePopulate);
    }
    bool pageToLast(std::function<void()> beforePopulate = nullptr)
    {
        return !isPageToLastDisabled() && paginate(page_count, beforePopulate);
    }
    void select(int idx)
    {
        selected_idx = idx;
        if (idx != -1)
            selected = &list[(page * pageSize) + idx];
    }
    // std functions for ui event listeners
    void toggleSort()
    {
        toggleDesc();
    }
    void refresh()
    {
        fetchUpdate();
    }
    void gotoFirst()
    {
        pageTo(0);
    }
    void gotoLast()
    {
        pageTo(page_count);
    }
    void prevOrLoad()
    {
        if (0 != page)
            pageTo(page - 1);
        else if (!desc_)
            fetchOlder();
        else
            fetchNewer();
    }
    void nextOrLoad()
    {
        if (page_count != page)
            pageTo(page + 1);
        else if (!desc_)
            fetchNewer();
        else
            fetchOlder();
    }
    
    // from ts verson
    /*int populate(SelectionType type, SelectionFlags flags,
            bool main, int idxSelected, int page)
    {
        return 0;
    }*/
};

} // coreds
