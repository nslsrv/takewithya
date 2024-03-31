#include "info.h"
#include "madvise.h"
#include "defaults.h"

#include <util/generic/buffer.h>
#include <util/generic/yexception.h>
#include <util/generic/singleton.h>

#if defined(_win_)
#include "winint.h"
#elif defined(_unix_)
#include <sys/types.h>
#include <sys/mman.h>

#ifndef MAP_NOCORE
#define MAP_NOCORE 0
#endif
#else
#error todo
#endif

#include <util/generic/utility.h>
#include <util/system/sanitizers.h>
#include "filemap.h"

#undef PAGE_SIZE
#undef GRANULARITY

#ifdef _win_
#define MAP_FAILED ((void*)(LONG_PTR)-1)
#endif

namespace {
    struct TSysInfo {
        inline TSysInfo()
            : GRANULARITY_(CalcGranularity())
            , PAGE_SIZE_(NSystemInfo::GetPageSize())
        {
        }

        static inline const TSysInfo& Instance() {
            return *Singleton<TSysInfo>();
        }

        static inline size_t CalcGranularity() noexcept {
#if defined(_win_)
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            return sysInfo.dwAllocationGranularity;
#else
            return NSystemInfo::GetPageSize();
#endif
        }

        const size_t GRANULARITY_;
        const size_t PAGE_SIZE_;
    };
}

#define GRANULARITY (TSysInfo::Instance().GRANULARITY_)
#define PAGE_SIZE (TSysInfo::Instance().PAGE_SIZE_)

const TString TMemoryMapCommon::UnknownFileName("Unknown_file_name");

static inline i64 DownToGranularity(i64 offset) noexcept {
    return offset & ~((i64)(GRANULARITY - 1));
}

// maybe we should move this function to another .cpp file to avoid unwanted optimization?
static void PrechargeImpl(TFile f, void* data, size_t dataSize, size_t off, size_t size) {
    if (off > dataSize) {
        assert(false);
        return;
    }
    size_t endOff = (size == (size_t)-1 ? dataSize : off + size);
    if (endOff > dataSize) {
        assert(false);
        endOff = dataSize;
    }
    size = endOff - off;
    if (dataSize == 0 || size == 0)
        return;

    volatile const char *c = (char*)data + off, *e = c + size;
#ifdef _freebsd_
    if (off % PAGE_SIZE) {
        off = off / PAGE_SIZE * PAGE_SIZE; // align on PAGE_SIZE
        size = endOff - off;
        c = (char*)data + off;
    }

    madvise((void*)c, e - c, MADV_WILLNEED);
    const size_t rdSize1 = 64 << 20, rdSize2 = 4 << 20;
    const size_t rdSize = size > rdSize2 * 32 ? rdSize1 : rdSize2;
    TArrayHolder<char> pages(new char[(rdSize + PAGE_SIZE - 1) / PAGE_SIZE]);
    TBuffer buf(Min(rdSize, size));
    ui32 nbufs = 0, nread = 0;
    for (size_t r = 0; r < size; r += rdSize) {
        bool needRead = true;
        size_t toRead = Min(rdSize, size - r);
        if (mincore((void*)(c + r), toRead, pages.Get()) != -1)
            needRead = memchr(pages.Get(), 0, (toRead + PAGE_SIZE - 1) / PAGE_SIZE) != 0;
        if (needRead)
            f.Pread(buf.Data(), toRead, off + r);
        madvise((void*)(c + r), toRead, MADV_WILLNEED);
        for (volatile const char* d = c; d < c + r; d += 512)
            *d;
        ++nbufs;
        nread += needRead;
    }
    //warnx("precharge: read %u/%u (blk %" PRISZT ")", nread, nbufs, rdSize);
    return;
#else
    Y_UNUSED(f);
#endif
    for (; c < e; c += 512)
        *c;
}

class TMemoryMap::TImpl: public TAtomicRefCount<TImpl> {
public:
    inline void CreateMapping() {
#if defined(_win_)
        Mapping_ = nullptr;
        if (Length_) {
            Mapping_ = CreateFileMapping(File_.GetHandle(), nullptr,
                                         (Mode_ & oAccessMask) == TFileMap::oRdOnly ? PAGE_READONLY : PAGE_READWRITE,
                                         (DWORD)(Length_ >> 32), (DWORD)(Length_ & 0xFFFFFFFF), nullptr);
            if (Mapping_ == nullptr) {
                ythrow yexception() << "Can't create file mapping of '" << DbgName_ << "': " << LastSystemErrorText();
            }
        } else {
            Mapping_ = MAP_FAILED;
        }
#elif defined(_unix_)
        if (!(Mode_ & oNotGreedy)) {
            PtrStart_ = mmap((caddr_t) nullptr, Length_,
                             ((Mode_ & oAccessMask) == oRdOnly) ? PROT_READ : PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_NOCORE, File_.GetHandle(), 0);
            if ((MAP_FAILED == PtrStart_) && Length_)
                ythrow yexception() << "Can't map " << (unsigned long)Length_ << " bytes of file '" << DbgName_ << "' at offset 0: " << LastSystemErrorText();
        } else
            PtrStart_ = nullptr;
#endif
    }

    void CheckFile() const {
        if (!File_.IsOpen()) {
            ythrow yexception() << "TMemoryMap: FILE '" << DbgName_ << "' is not open, " << LastSystemErrorText();
        }
        if (Length_ < 0) {
            ythrow yexception() << "'" << DbgName_ << "' is not a regular file";
        }
    }

    inline TImpl(FILE* f, EOpenMode om, TString dbgName)
        : File_(Duplicate(f))
        , DbgName_(std::move(dbgName))
        , Length_(File_.GetLength())
        , Mode_(om)
    {
        CheckFile();
        CreateMapping();
    }

    inline TImpl(const TString& name, EOpenMode om)
        : File_(name, (om & oRdWr) ? OpenExisting | RdWr : OpenExisting | RdOnly)
        , DbgName_(name)
        , Length_(File_.GetLength())
        , Mode_(om)
    {
        CheckFile();
        CreateMapping();
    }

    inline TImpl(const TString& name, i64 length, EOpenMode om)
        : File_(name, (om & oRdWr) ? OpenExisting | RdWr : OpenExisting | RdOnly)
        , DbgName_(name)
        , Length_(length)
        , Mode_(om)
    {
        CheckFile();

        if (File_.GetLength() < Length_) {
            File_.Resize(Length_);
        }

        CreateMapping();
    }

    inline TImpl(const TFile& file, EOpenMode om, TString dbgName)
        : File_(file)
        , DbgName_(File_.GetName() ? File_.GetName() : std::move(dbgName))
        , Length_(File_.GetLength())
        , Mode_(om)
    {
        CheckFile();
        CreateMapping();
    }

    inline bool IsOpen() const noexcept {
        return File_.IsOpen()
#if defined(_win_)
               && Mapping_ != nullptr
#endif
            ;
    }

    inline bool IsWritable() const noexcept {
        return (Mode_ & oRdWr);
    }

    inline TMapResult Map(i64 offset, size_t size) {
        assert(File_.IsOpen());

        if (offset > Length_) {
            ythrow yexception() << "Can't map something at offset " << offset << " of '" << DbgName_ << "' with length " << Length_;
        }

        if (offset + (i64)size > Length_) {
            ythrow yexception() << "Can't map " << (unsigned long)size << " bytes at offset " << offset << " of '" << DbgName_ << "' with length " << Length_;
        }

        TMapResult result;

        i64 base = DownToGranularity(offset);
        result.Head = (i32)(offset - base);
        size += result.Head;

#if defined(_win_)
        result.Ptr = MapViewOfFile(Mapping_, (Mode_ & oAccessMask) == oRdOnly ? FILE_MAP_READ : FILE_MAP_WRITE,
                                   HI_32(base), LO_32(base), size);
#else
#if defined(_unix_)
        if (Mode_ & oNotGreedy) {
#endif
            result.Ptr = mmap((caddr_t) nullptr, size, (Mode_ & oAccessMask) == oRdOnly ? PROT_READ : PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_NOCORE,
                              File_.GetHandle(), base);

            if (result.Ptr == (char*)(-1)) {
                result.Ptr = nullptr;
            }
#if defined(_unix_)
        } else {
            result.Ptr = PtrStart_ ? static_cast<caddr_t>(PtrStart_) + base : nullptr;
        }
#endif
#endif
        if (result.Ptr != nullptr || size == 0) { // allow map of size 0
            result.Size = size;
        } else {
            ythrow yexception() << "Can't map " << (unsigned long)size << " bytes at offset " << offset << " of '" << DbgName_ << "': " << LastSystemErrorText();
        }
        NSan::Unpoison(result.Ptr, result.Size);
        if (Mode_ & oPrecharge) {
            PrechargeImpl(File_, result.Ptr, result.Size, 0, result.Size);
        }

        return result;
    }

#if defined(_win_)
    inline bool Unmap(void* ptr, size_t) {
        return ::UnmapViewOfFile(ptr) != FALSE;
    }
#else
    inline bool Unmap(void* ptr, size_t size) {
#if defined(_unix_)
        if (Mode_ & oNotGreedy)
#endif
            return size == 0 || ::munmap(static_cast<caddr_t>(ptr), size) == 0;
#if defined(_unix_)
        else
            return true;
#endif
    }
#endif

    void SetSequential() {
#if defined(_unix_)
        if (!(Mode_ & oNotGreedy) && Length_) {
            MadviseSequentialAccess(PtrStart_, Length_);
        }
#endif
    }

    void Evict(void* ptr, size_t len) {
        MadviseEvict(ptr, len);
    }

    void Evict() {
#if defined(_unix_)
//        Evict(PtrStart_, Length_);
#endif
    }

    inline ~TImpl() {
#if defined(_win_)
        if (Mapping_) {
            ::CloseHandle(Mapping_); // != FALSE
            Mapping_ = nullptr;
        }
#elif defined(_unix_)
        if (PtrStart_) {
            munmap((caddr_t)PtrStart_, Length_);
        }
#endif
    }

    inline i64 Length() const noexcept {
        return Length_;
    }

    inline TFile GetFile() const noexcept {
        return File_;
    }

    inline TString GetDbgName() const {
        return DbgName_;
    }

    inline EOpenMode GetMode() const noexcept {
        return Mode_;
    }

private:
    TFile File_;
    TString DbgName_; // This string is never used to actually open a file, only in exceptions
    i64 Length_;
    EOpenMode Mode_;

#if defined(_win_)
    void* Mapping_;
#elif defined(_unix_)
    void* PtrStart_;
#endif
};

TMemoryMap::TMemoryMap(const TString& name)
    : Impl_(new TImpl(name, EOpenModeFlag::oRdOnly))
{
}

TMemoryMap::TMemoryMap(const TString& name, EOpenMode om)
    : Impl_(new TImpl(name, om))
{
}

TMemoryMap::TMemoryMap(const TString& name, i64 length, EOpenMode om)
    : Impl_(new TImpl(name, length, om))
{
}

TMemoryMap::TMemoryMap(FILE* f, TString dbgName)
    : Impl_(new TImpl(f, EOpenModeFlag::oRdOnly, std::move(dbgName)))
{
}

TMemoryMap::TMemoryMap(FILE* f, EOpenMode om, TString dbgName)
    : Impl_(new TImpl(f, om, std::move(dbgName)))
{
}

TMemoryMap::TMemoryMap(const TFile& file, TString dbgName)
    : Impl_(new TImpl(file, EOpenModeFlag::oRdOnly, std::move(dbgName)))
{
}

TMemoryMap::TMemoryMap(const TFile& file, EOpenMode om, TString dbgName)
    : Impl_(new TImpl(file, om, std::move(dbgName)))
{
}

TMemoryMap::~TMemoryMap() = default;

TMemoryMap::TMapResult TMemoryMap::Map(i64 offset, size_t size) {
    return Impl_->Map(offset, size);
}

bool TMemoryMap::Unmap(void* ptr, size_t size) {
    return Impl_->Unmap(ptr, size);
}

bool TMemoryMap::Unmap(TMapResult region) {
    return Unmap(region.Ptr, region.Size);
}

void TMemoryMap::ResizeAndReset(i64 size) {
    EOpenMode om = Impl_->GetMode();
    TFile file = GetFile();
    file.Resize(size);
    Impl_.Reset(new TImpl(file, om, Impl_->GetDbgName()));
}

TMemoryMap::TMapResult TMemoryMap::ResizeAndRemap(i64 offset, size_t size) {
    ResizeAndReset(offset + (i64)size);
    return Map(offset, size);
}

void TMemoryMap::SetSequential() {
    Impl_->SetSequential();
}

void TMemoryMap::Evict(void* ptr, size_t len) {
    Impl_->Evict(ptr, len);
}

void TMemoryMap::Evict() {
    Impl_->Evict();
}

i64 TMemoryMap::Length() const noexcept {
    return Impl_->Length();
}

bool TMemoryMap::IsOpen() const noexcept {
    return Impl_->IsOpen();
}

bool TMemoryMap::IsWritable() const noexcept {
    return Impl_->IsWritable();
}

TFile TMemoryMap::GetFile() const noexcept {
    return Impl_->GetFile();
}

TFileMap::TFileMap(const TMemoryMap& map) noexcept
    : Map_(map)
{
}

TFileMap::TFileMap(const TString& name)
    : Map_(name)
{
}

TFileMap::TFileMap(const TString& name, EOpenMode om)
    : Map_(name, om)
{
}

TFileMap::TFileMap(const TString& name, i64 length, EOpenMode om)
    : Map_(name, length, om)
{
}

TFileMap::TFileMap(FILE* f, EOpenMode om, TString dbgName)
    : Map_(f, om, dbgName)
{
}

TFileMap::TFileMap(const TFile& file, EOpenMode om, TString dbgName)
    : Map_(file, om, dbgName)
{
}

TFileMap::TFileMap(const TFileMap& fm) noexcept
    : Map_(fm.Map_)
{
}

void TFileMap::Flush(void* ptr, size_t size, bool sync) {
    Y_ASSERT(ptr >= Ptr());
    Y_ASSERT(static_cast<char*>(ptr) + size <= static_cast<char*>(Ptr()) + MappedSize());

    if (!Region_.IsMapped()) {
        return;
    }

#if defined(_win_)
    if (sync) {
        FlushViewOfFile(ptr, size);
    }
#else
    msync(ptr, size, sync ? MS_SYNC : MS_ASYNC);
#endif
}

TFileMap::TMapResult TFileMap::Map(i64 offset, size_t size) {
    Unmap();
    Region_ = Map_.Map(offset, size);
    return Region_;
}

TFileMap::TMapResult TFileMap::ResizeAndRemap(i64 offset, size_t size) {
    // explicit Unmap() is required because in oNotGreedy mode the Map_ object doesn't own the mapped area
    Unmap();
    Region_ = Map_.ResizeAndRemap(offset, size);
    return Region_;
}

void TFileMap::Unmap() {
    if (!Region_.IsMapped()) {
        return;
    }

    if (Map_.Unmap(Region_)) {
        Region_.Reset();
    } else {
        ythrow yexception() << "can't unmap file";
    }
}

TFileMap::~TFileMap() {
    try {
        // explicit Unmap() is required because in oNotGreedy mode the Map_ object doesn't own the mapped area
        Unmap();
    } catch (...) {
    }
}

void TFileMap::Precharge(size_t pos, size_t size) const {
    PrechargeImpl(GetFile(), Ptr(), MappedSize(), pos, size);
}

/******************************** MappedFile ********************************/

TMappedFile::TMappedFile(TFileMap* map, const char* dbgName) {
    Map_ = map;
    i64 len = Map_->Length();
    if (HI_32(len) != 0 && sizeof(size_t) <= sizeof(ui32))
        ythrow yexception() << "File '" << dbgName << "' mapping error: " << len << " too large";

    Map_->Map(0, static_cast<size_t>(len));
}

TMappedFile::TMappedFile(const TFile& file, TFileMap::EOpenMode om, const char* dbgName)
    : Map_(nullptr)
{
    init(file, om, dbgName);
}

void TMappedFile::precharge(size_t off, size_t size) const {
    if (!Map_)
        return;

    Map_->Precharge(off, size);
}

void TMappedFile::init(const TString& name) {
    THolder<TFileMap> map(new TFileMap(name));
    TMappedFile newFile(map.Get(), ~name);
    map.Release();
    newFile.swap(*this);
    newFile.term();
}

void TMappedFile::init(const TString& name, size_t length, TFileMap::EOpenMode om) {
    THolder<TFileMap> map(new TFileMap(name, length, om));
    TMappedFile newFile(map.Get(), ~name);
    map.Release();
    newFile.swap(*this);
    newFile.term();
}

void TMappedFile::init(const TFile& file, TFileMap::EOpenMode om, const char* dbgName) {
    THolder<TFileMap> map(new TFileMap(file, om));
    TMappedFile newFile(map.Get(), dbgName);
    map.Release();
    newFile.swap(*this);
    newFile.term();
}

void TMappedFile::init(const TString& name, TFileMap::EOpenMode om) {
    THolder<TFileMap> map(new TFileMap(name, om));
    TMappedFile newFile(map.Get(), ~name);
    map.Release();
    newFile.swap(*this);
    newFile.term();
}

void TMappedFile::flush() {
    Map_->Flush();
}

TMappedAllocation::TMappedAllocation(size_t size, bool shared, void* addr)
    : Ptr_(nullptr)
    , Size_(0)
    , Shared_(shared)
#if defined(_win_)
    , Mapping_(nullptr)
#endif
{
    if (size != 0)
        Alloc(size, addr);
}

void* TMappedAllocation::Alloc(size_t size, void* addr) {
    assert(Ptr_ == nullptr);
#if defined(_win_)
    (void)addr;
    Mapping_ = CreateFileMapping((HANDLE)-1, nullptr, PAGE_READWRITE, 0, size ? size : 1, nullptr);
    Ptr_ = MapViewOfFile(Mapping_, FILE_MAP_WRITE, 0, 0, size ? size : 1);
#else
    Ptr_ = mmap(addr, size, PROT_READ | PROT_WRITE, (Shared_ ? MAP_SHARED : MAP_PRIVATE) | MAP_ANON, -1, 0);

    if (Ptr_ == (void*)MAP_FAILED) {
        Ptr_ = nullptr;
    }
#endif
    if (Ptr_ != nullptr)
        Size_ = size;
    return Ptr_;
}

void TMappedAllocation::Dealloc() {
    if (Ptr_ == nullptr)
        return;
#if defined(_win_)
    UnmapViewOfFile(Ptr_);
    CloseHandle(Mapping_);
    Mapping_ = nullptr;
#else
    munmap((caddr_t)Ptr_, Size_);
#endif
    Ptr_ = nullptr;
    Size_ = 0;
}

void TMappedAllocation::swap(TMappedAllocation& with) {
    DoSwap(Ptr_, with.Ptr_);
    DoSwap(Size_, with.Size_);
#if defined(_win_)
    DoSwap(Mapping_, with.Mapping_);
#endif
}
