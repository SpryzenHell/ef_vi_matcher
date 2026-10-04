    MappedRegion(MappedRegion&& o) noexcept{move_from(std::move(o));}
    MappedRegion& operator=(MappedRegion&& o) noexcept{if(this!=&o){reset();move_from(std::move(o));}return *this;}
    [[nodiscard]] void* data() noexcept{return base_;} [[nodiscard]] const void* data() const noexcept{return base_;}
    [[nodiscard]] std::size_t size() const noexcept{return length_;} [[nodiscard]] bool valid() const noexcept{return base_!=nullptr;}
    [[nodiscard]] bool hugepage_backed() const noexcept{return hugepage_backed_;} [[nodiscard]] std::size_t page_size() const noexcept{return page_size_;}
    void reset() noexcept{if(!base_)return; if(mmaped_)(void)::munmap(base_,length_);else std::free(base_); base_=nullptr;length_=0;mmaped_=false;hugepage_backed_=false;page_size_=0;}
private:
    void move_from(MappedRegion&&o) noexcept{base_=o.base_;length_=o.length_;mmaped_=o.mmaped_;hugepage_backed_=o.hugepage_backed_;page_size_=o.page_size_;o.base_=nullptr;o.length_=0;o.mmaped_=false;o.hugepage_backed_=false;o.page_size_=0;}
    void* base_{nullptr}; std::size_t length_{0}; bool mmaped_{false}; bool hugepage_backed_{false}; std::size_t page_size_{0};
};
inline std::size_t round_up(std::size_t n,std::size_t a){return ((n+a-1)/a)*a;}
inline MappedRegion map_pages(std::size_t bytes,PageMode mode,bool strict){
    if (bytes == 0) throw std::invalid_argument("cannot map a zero-byte region");
    const auto normal_page=static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const auto try_map=[&](std::size_t page_size,int flags)->MappedRegion{
        const auto len=round_up(bytes,page_size); void* p=::mmap(nullptr,len,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|flags,-1,0);
        if(p==MAP_FAILED) return {};
        return {p,len,true,true,page_size};
    };
    if(mode==PageMode::Huge1G || (mode==PageMode::Auto && bytes >= (1ULL<<30))){auto r=try_map(1ULL<<30,MAP_HUGETLB|MAP_HUGE_1GB);if(r.valid())return r;if(mode==PageMode::Huge1G&&strict)throw std::runtime_error("1 GiB HUGETLB mapping failed; provision 1 GiB huge pages first");}
    if(mode==PageMode::Huge2M || (mode==PageMode::Auto && bytes >= (2ULL<<20))){auto r=try_map(2ULL<<20,MAP_HUGETLB|MAP_HUGE_2MB);if(r.valid())return r;if(mode==PageMode::Huge2M&&strict)throw std::runtime_error("2 MiB HUGETLB mapping failed; provision 2 MiB huge pages first");}
    if(mode==PageMode::Huge1G&&strict)throw std::runtime_error("strict 1 GiB huge-page mode requested but no mapping is available");
    if(mode==PageMode::Huge2M&&strict)throw std::runtime_error("strict 2 MiB huge-page mode requested but no mapping is available");
    const auto len=round_up(bytes,normal_page); void* p=nullptr; if(::posix_memalign(&p,64,len)!=0)throw std::bad_alloc(); return {p,len,false,false,normal_page};
}
template<typename T> class FixedPool final {
    struct FreeNode{FreeNode*next;};
    static constexpr std::size_t kStride=((sizeof(T)>sizeof(FreeNode)?sizeof(T):sizeof(FreeNode))+alignof(T)-1)/alignof(T)*alignof(T);
public:
    explicit FixedPool(std::size_t capacity,PageMode mode=PageMode::Auto,bool strict=false)
        : capacity_(capacity), stride_(kStride), region_(make_region(capacity, mode, strict)) {
        if(capacity_==0)throw std::invalid_argument("FixedPool capacity must be positive");
        auto* bytes=static_cast<std::byte*>(region_.data());
        for(std::size_t i=0;i<capacity_;++i){auto* node=reinterpret_cast<FreeNode*>(bytes+i*stride_);node->next=(i+1<capacity_)?reinterpret_cast<FreeNode*>(bytes+(i+1)*stride_):nullptr;}
        free_head_=reinterpret_cast<FreeNode*>(bytes);
    }
    FixedPool(const FixedPool&)=delete; FixedPool& operator=(const FixedPool&)=delete;
    template<typename...Args> [[nodiscard]] T* create(Args&&...args){void* memory=allocate();if(!memory)return nullptr;try{return ::new(memory)T(std::forward<Args>(args)...);}catch(...){deallocate(memory);throw;}}
    void destroy(T* object) noexcept{if(!object)return;object->~T();deallocate(object);}
    [[nodiscard]] void* allocate() noexcept{if(!free_head_)return nullptr;auto*out=free_head_;free_head_=free_head_->next;++used_;return out;}
    void deallocate(void*p) noexcept{if(!p)return;auto*node=static_cast<FreeNode*>(p);node->next=free_head_;free_head_=node;--used_;}
    [[nodiscard]] std::size_t capacity()const noexcept{return capacity_;} [[nodiscard]] std::size_t used()const noexcept{return used_;} [[nodiscard]] std::size_t free_count()const noexcept{return capacity_-used_;}
    [[nodiscard]] const void* base()const noexcept{return region_.data();} [[nodiscard]] std::size_t mapped_bytes()const noexcept{return region_.size();}
    [[nodiscard]] bool hugepage_backed()const noexcept{return region_.hugepage_backed();} [[nodiscard]] std::size_t page_size()const noexcept{return region_.page_size();}
    [[nodiscard]] static constexpr std::size_t stride()noexcept{return kStride;}
private:
    static MappedRegion make_region(std::size_t capacity, PageMode mode, bool strict) {
        if (capacity == 0) throw std::invalid_argument("FixedPool capacity must be positive");
        return map_pages(kStride * capacity, mode, strict);
    }
