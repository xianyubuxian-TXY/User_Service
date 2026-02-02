#pragma once

namespace user_service{

// 分页查询参数
struct PageParams {
    int page = 1;           // 当前页
    int page_size = 20;     // 每页记录数

    // 计算 offset
    int Offset() const{
        return (page-1)*page_size;
    }

    // 参数校验 
    void Validate(){
        if(page<1) page=1;
        if(page_size<1) page_size=20;
        if(page_size>100) page_size=100; // 防止一次查太多,导致性能太差
    }
};

// 分页查询结果
struct PageResult {
    int total_records=0;  // 总记录数        
    int total_pages=0;    // 总页数
    int page=1;           // 当前页
    int page_size=20;      // 每页记录数量

    static PageResult Create(int page,int page_size,int total_records){
        PageResult res;
        res.page=page;
        res.page_size=page_size;
        res.total_records=total_records;
        res.total_pages=(total_records+page_size-1)/page_size; //上取整
        return res;
    }
};

}
