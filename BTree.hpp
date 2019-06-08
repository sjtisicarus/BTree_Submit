#include "utility.hpp"
#include <functional>
#include <cstddef>
#include "exception.hpp"
#include <map>
#include <time.h>
namespace sjtu {
    template <class Key, class Value, class Compare = std::less<Key> >
    class BTree {
    public:
        typedef std::pair< Key, Value> value_type;

        class iterator;
        class const_iterator;

    private:
        static const int M = (4079 / (sizeof(off_t) + sizeof(Key))) < 5 ? 4 : (4079 / (sizeof(off_t) + sizeof(Key)) - 1);
        static const int L = (4076 / (sizeof(value_type))) < 5 ? 4 : (4076 / (sizeof(value_type)) - 1);
        //offset_info = 0;
		char Filename[50];// = "test.sjtu";
        FILE* fp;
        bool isopen;

        off_t  recycleStacks[10000];
        int remain;//重复利用空的块

        struct index{
            size_t head;
            size_t tail;
            size_t root;
            size_t size;
            off_t len;
            index() {
                head = 0;
                tail = 0;
                root = 0;
                size = 0;
                len = 0;
            }
        }info;
        struct leafnode{
            off_t offset;
            off_t parent;
            off_t next,prev;
            int cnt;
            value_type data[L+1];
            leafnode(){
                offset=parent=next=prev=cnt=0;
            }
        };
        struct branchnode{
            off_t offset;
            off_t parent;
            off_t child[M+1];
            Key keys[M+1];
            int cnt;
            bool kidisleaf;
            branchnode(){
                offset=parent=cnt=0;
                kidisleaf=false;
            }
        };

        inline bool fileopen(){
            if (!isopen) {
                fp = fopen(Filename, "rb+");
                isopen=1;
                if (fp == nullptr) {
                    fp = fopen(Filename, "w");
                    fclose(fp);
                    fp = fopen(Filename, "rb+");
                    return false;              //init needed
                }
                else {
                    fileread(&info, 0, 1, sizeof(index));
                    return true;               //normally open
                }
            }
			return false;
        }
        inline void fileclose(){
            if(isopen){
                isopen=0;
                fclose(fp);
            }
        }
        inline void fileread(void *node, off_t offset, size_t num, size_t size) const {
            if (fseek(fp,offset,SEEK_SET)) throw "open file failed!";
            fread(node,num,size,fp);
        }
        inline void filewrite(void *place, off_t offset, size_t num, size_t size) const {
            if (fseek(fp, offset, SEEK_SET)) throw "open file failed!";
            fwrite(place, num, size, fp);
        }

        void initree(){
            branchnode root;
            leafnode leaf;
            remain=0;
            info.size = 0;
            info.root += sizeof(index);
            root.offset += sizeof(index);
            info.head += root.offset + sizeof(branchnode);
            info.tail += root.offset + sizeof(branchnode);
            leaf.offset = root.offset + sizeof(branchnode);
            info.len = leaf.offset+sizeof(leafnode);
            root.parent = 0;
            root.cnt = 1;
            root.kidisleaf = 1;
            root.child[0] = leaf.offset;
            leaf.parent = root.offset;
            leaf.next = leaf.prev = 0;
            leaf.cnt = 0;
            filewrite(&info, 0, 1, sizeof(index));
            filewrite(&root, root.offset, 1, sizeof(branchnode));
            filewrite(&leaf, leaf.offset, 1, sizeof(leafnode));
        }
        off_t findpos(const Key key,size_t bgn){
            branchnode node;
            fileread(&node,bgn,1,sizeof(branchnode));
            int pos=0;
            while(pos<node.cnt&&node.keys[pos]<=key)
                ++pos;
            if(pos==0)return 0;
            if(node.kidisleaf)
                return node.child[pos-1];
            else
                return findpos(key,node.child[pos-1]);
        }
        void update(off_t par, const Key &key,off_t kid){
            branchnode dad ;
            fileread(&dad,par,1,sizeof(branchnode));
            int pos = 0;
            while(pos < dad.cnt && dad.keys[pos] <= key)
                pos++;
            dad.cnt++;
            for (int i = dad.cnt - 1; i > pos; --i)
                dad.keys[i] = dad.keys[i-1];
            for (int i = dad.cnt - 1; i > pos; --i)
                dad.child[i] = dad.child[i-1];
            dad.keys[pos]=key;
            dad.child[pos]=kid;
            if(dad.cnt > M)split_node(dad);
            else
                filewrite(&dad,dad.offset,1,sizeof(branchnode));
        }

        void  insert_leaf(leafnode &leaf,const Key &key,const Value &value, std::pair < iterator, OperationResult >&ans){
            int pos=0;
            while( pos < leaf.cnt && leaf.data[pos].first <= key ){
				if (key == leaf.data[pos].first)
				{
					ans.second = Fail;
					return;
				}
                pos++;
            }
            info.size++;
            leaf.cnt++;
            for(int i = leaf.cnt -1;i > pos;--i)
            {
                leaf.data[i].first = leaf.data[i-1].first;
                leaf.data[i].second = leaf.data[i-1].second;
            }
            leaf.data[pos].first=key;
            leaf.data[pos].second=value;
            ans.first.from = this;
            ans.first.cur = pos;
            ans.first.leafhead = leaf.offset;
            filewrite(&info ,0,1,sizeof(leafnode));
            if(leaf.cnt <= L)   filewrite( &leaf, leaf.offset,1,sizeof(leafnode));
            else split_leaf(leaf,ans.first,key);
			ans.second = Success;
        }
        void split_leaf(leafnode &leaf,iterator &it,const Key &key){
            leafnode part;
            part.cnt = (leaf.cnt+1)>>1;
            leaf.cnt = leaf.cnt >> 1;
            if(remain){
                part.offset = recycleStacks[remain];
                remain--;
            }
            else{
                part.offset = info.len;
                info.len += sizeof(leafnode);
            }
            it.leafhead = part.offset;
            part.parent = leaf.parent;
            for(int i=0;i<part.cnt;++i){
                part.data[i].first = leaf.data[i+leaf.cnt].first;
                part.data[i].second = leaf.data[i+leaf.cnt].second;
                if(part.data[i].first == key){
                    it.leafhead = part.offset;
                    it.cur = i;
                }
            }
            part.prev=leaf.offset;
            part.next=leaf.next;
            leaf.next=part.offset;
            leafnode nxt;
            if(part.next){
                fileread(&nxt, part.next, 1, sizeof(leafnode));
                nxt.prev = part.offset;
                filewrite(&nxt, part.next, 1, sizeof(leafnode));
            }
            //update tail
            if(leaf.offset == info.tail)
                info.tail = part.offset;
            filewrite(&leaf,leaf.offset,1,sizeof(leafnode));
            filewrite(&part,part.offset,1,sizeof(leafnode));
            filewrite(&info,0,1,sizeof(index));

            update(leaf.parent,part.data[0].first,part.offset);
        }
        void split_node(branchnode &node){
            branchnode part;
            part.cnt = (node.cnt+1)>>1;
            node.cnt >>= 1;
            if(remain){
                part.offset = recycleStacks[remain];
                remain--;
            }
            else{
                part.offset = info.len;
                info.len += sizeof(leafnode);
            }
            part.kidisleaf=node.kidisleaf;
            //update child
            for (int i = 0; i < part.cnt; ++i){
                part.child[i] = node.child[i + node.cnt];
                part.keys[i] = node.keys[i + node.cnt];
            }
            for (int i = 0; i < part.cnt; ++i) {
                if(part.kidisleaf) {
                    leafnode son;
                    fileread(&son,part.child[i], 1, sizeof(leafnode));
                    son.parent = part.offset;
                    filewrite(&son, son.offset, 1, sizeof(leafnode));
                }
                else {
                    branchnode son;
                    fileread(&son, son.child[i], 1, sizeof(branchnode));
                    son.parent = part.offset;
                    filewrite(&son, son.offset, 1, sizeof(branchnode));
                }
            }
            //update parent
            part.parent=node.parent;
            if(node.offset == info.root) {
                branchnode newroot;
                newroot.parent = 0;
                newroot.offset = info.len;
                info.len += sizeof(branchnode);
                newroot.cnt = 2;
                newroot.keys[0] = node.keys[0];
                newroot.keys[1] = part.keys[0];
                newroot.child[0] = node.offset;
                newroot.child[1] = part.offset;
                newroot.kidisleaf = 0;
                node.parent = newroot.offset;
                part.parent = newroot.offset;
                info.root = newroot.offset;
                filewrite(&newroot, newroot.offset, 1, sizeof(branchnode));
            }
            else{
                update(part.parent,part.keys[0],part.offset);
            }

            filewrite(&node, node.offset, 1, sizeof(branchnode));
            filewrite(&part, part.offset, 1, sizeof(branchnode));
            filewrite(&info, 0, 1, sizeof(index));
        }
        // Your private members go here
    public:


        class iterator {
            friend class BTree;
        private:
            off_t leafhead;
            int cur;
            BTree * from;
        public:
            bool modify(const Value& value){
                leafnode node;
                from -> fileread(&node, leafhead, 1, sizeof(leafnode));
                if(cur>node.cnt)return false;
                node.data[cur].second = value;
                from -> filewrite(&node, leafhead, 1, sizeof(leafnode));
                return true;
            }
            Value getValue() {
                leafnode p;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                return p.data[cur].second;
            }
            iterator() {
                from= nullptr;
                leafhead=cur=0;
            }
            iterator(const iterator& other) {
                leafhead=other.leafhead;
                cur=other.cur;
                from=other.from;
            }
            iterator(const const_iterator& other) {
                leafhead=other.leafhead;
                cur=other.cur;
                from=other.from;
            }
            iterator(BTree * a,off_t b,int c){
                from=a;
                leafhead=b;
                cur=c;
            }
            // Return a new iterator which points to the n-next elements
            iterator operator++(int) {
                iterator ans = *this;
                if(*this == from -> end()) {
                    from = nullptr;
                    leafhead = 0;
                    cur = 0;
                    return ans;
                }
                leafnode p;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                if(cur == p.cnt - 1) {
                        leafhead = p.next;
                        cur = 0;
                } else ++ cur;
                return ans;
            }
            iterator& operator++() {
                if(*this == from -> end()) {
                    from = nullptr;
                    leafhead = 0;
                    cur = 0;
                    return *this;
                }
                leafnode p;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                if(cur == p.cnt - 1) {
                        leafhead = p.next;
                        cur = 0;
                } else ++ cur;
                return *this;
            }
            iterator operator--(int) {
                iterator ans = *this;
                if(*this == from -> begin()) {
                    from = nullptr;
                    leafhead = 0;
                    cur = 0;
                    return ans;
                }
                leafnode p, q;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                if(cur == 0) {
                    leafhead = p.pre;
                    from -> fileread(&q, p.prev, 1, sizeof(leafnode));
                    cur = q.cnt - 1;
                } else -- cur;
                return ans;
            }
            iterator& operator--() {
                if(*this == from -> begin()) {
                    from = nullptr;
                    leafhead = 0;
                    cur = 0;
                    return *this;
                }
                leafnode p, q;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                if(cur == 0) {
                    leafhead = p.pre;
                    from -> fileread(&q, p.prev, 1, sizeof(leafnode));
                    cur = q.cnt - 1;
                } else -- cur;
                return *this;
            }
            // Overloaded of operator '==' and '!='
            // Check whether the iterators are same
            bool operator==(const iterator& rhs) const {
                return leafhead == rhs.leafhead && cur == rhs.cur && from == rhs.from;
            }
            bool operator==(const const_iterator& rhs) const {
                return leafhead == rhs.leafhead && cur == rhs.cur && from == rhs.from;
            }
            bool operator!=(const iterator& rhs) const {
                return leafhead != rhs.leafhead || cur != rhs.cur || from != rhs.from;
            }
            bool operator!=(const const_iterator& rhs) const {
                return leafhead != rhs.leafhead || cur != rhs.cur || from != rhs.from;
            }
        };
        class const_iterator {
            friend class BTree;
        private:
            off_t leafhead;
            int cur;
            BTree * from;
        public:
            Value getValue() {
                leafnode p;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                return p.data[cur].second;
            }
            const_iterator() {
                from= nullptr;
                leafhead=cur=0;
            }
            const_iterator(const iterator& other) {
                leafhead=other.leafhead;
                cur=other.cur;
                from=other.from;
            }
            const_iterator(const const_iterator& other) {
                leafhead=other.leafhead;
                cur=other.cur;
                from=other.from;
            }
            const_iterator(BTree * a,off_t b,int c){
                from=a;
                leafhead=b;
                cur=c;
            }
            // Return a new iterator which points to the n-next elements
            const_iterator operator++(int) {
                iterator ans = *this;
                if(*this == from -> end()) {
                    from = nullptr;
                    leafhead = 0;
                    cur = 0;
                    return ans;
                }
                leafnode p;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                if(cur == p.cnt - 1) {
                    leafhead = p.next;
                    cur = 0;
                } else ++ cur;
                return ans;
            }
            const_iterator& operator++() {
                if(*this == from -> end()) {
                    from = nullptr;
                    leafhead = 0;
                    cur = 0;
                    return *this;
                }
                leafnode p;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                if(cur == p.cnt - 1) {
                    leafhead = p.next;
                    cur = 0;
                } else ++ cur;
                return *this;
            }
            const_iterator operator--(int) {
                iterator ans = *this;
                if(*this == from -> begin()) {
                    from = nullptr;
                    leafhead = 0;
                    cur = 0;
                    return ans;
                }
                leafnode p, q;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                if(cur == 0) {
                    leafhead = p.pre;
                    from -> fileread(&q, p.prev, 1, sizeof(leafnode));
                    cur = q.cnt - 1;
                } else -- cur;
                return ans;
            }
            const_iterator& operator--() {
                if(*this == from -> begin()) {
                    from = nullptr;
                    leafhead = 0;
                    cur = 0;
                    return *this;
                }
                leafnode p, q;
                from -> fileread(&p, leafhead, 1, sizeof(leafnode));
                if(cur == 0) {
                    leafhead = p.pre;
                    from -> fileread(&q, p.prev, 1, sizeof(leafnode));
                    cur = q.cnt - 1;
                } else -- cur;
                return *this;
            }
            // Overloaded of operator '==' and '!='
            // Check whether the iterators are same
            bool operator==(const iterator& rhs) const {
                return leafhead == rhs.leafhead && cur == rhs.cur && from == rhs.from;
            }
            bool operator==(const const_iterator& rhs) const {
                return leafhead == rhs.leafhead && cur == rhs.cur && from == rhs.from;
            }
            bool operator!=(const iterator& rhs) const {
                return leafhead != rhs.leafhead || cur != rhs.cur || from != rhs.from;
            }
            bool operator!=(const const_iterator& rhs) const {
                return leafhead != rhs.leafhead || cur != rhs.cur || from != rhs.from;
            }
        };
        // Default Constructor and Copy Constructor
        BTree() {
            time_t t;
            struct tm *p;
            t = time(NULL);
            p = gmtime(&t);
            sprintf(Filename, "%d-%d-%d-%d-%d-%d.txt",1900+p->tm_year,1+p->tm_mon,p->tm_mday,8+p->tm_hour,p->tm_min,p->tm_sec);
            fp = nullptr;
            if(!fileopen())
                initree();
        }
        BTree(const BTree& other) {
            // Todo Copy
        }
        BTree& operator=(const BTree& other) {
            // Todo Assignment
        }
        ~BTree() {
            fileclose();
        }
        // Insert: Insert certain Key-Value into the database
        // Return a std::pair, the first of the std::pair is the iterator point to the new
        // element, the second of the std::pair is Success if it is successfully inserted

        std::pair<iterator, OperationResult> insert(const Key& key, const Value& value) {
           off_t leafpos=findpos(key,info.root);
           leafnode leaf;
           if(info.size == 0 || leafpos == 0){
               fileread(&leaf,info.head,1, sizeof(leafnode));
			   std::pair<iterator, OperationResult>ans;
			   insert_leaf(leaf, key, value,ans);
               if(ans.second == Fail)
                   return ans;
               off_t offset=leaf.parent;
               branchnode node;
               while(offset != 0){
                   fileread(&node,offset,1,sizeof(branchnode));
                   node.keys[0] = key;
                   filewrite(&node,offset,1,sizeof(branchnode));
                   offset = node.parent;
               }
               return ans;
           }
           fileread(&leaf,leafpos,1,sizeof(leafnode));
		   std::pair<iterator, OperationResult>ans;
		   insert_leaf(leaf, key, value,ans);
           return ans;
        }
        // Erase: Erase the Key-Value
        // Return Success if it is successfully erased
        // Return Fail if the key doesn't exist in the database
        OperationResult erase(const Key& key) {
            /*
             *
            off_t leafpos = findpos( key , info.root );
            if(leafpos == 0)
                return Fail;
            leafnode leaf;
            fileread(&leaf, leafpos, 1,sizeof(leafnode));
            int pos = 0;
            while(pos<leaf.cnt && leaf.data[pos].first < key)
               pos++;

            leaf.cnt --;
            for (int i = pos; i < leaf.cnt; ++i){
                leaf.data[i].first = leaf.data[i+1].first;
                leaf.data[i].second = leaf.data[i+1].second;
            }
            //if small,merge it
            off_t ancestor = leaf.parent;
            branchnode node;
            */
            return Fail;
        }
        // Return a iterator to the beginning
        iterator begin() {  return iterator(this,info.head,0);  }
        const_iterator cbegin() const { return const_iterator(this,info.head,0);  }
        // Return a iterator to the end(the next element after the last)
        iterator end() {
            leafnode last;
            fileread(&last, info.tail, 1, sizeof(leafnode));
            return iterator(this, info.tail, last.cnt);
        }
        const_iterator cend() const {
            leafnode last;
            fileread(&last, info.tail, 1, sizeof(leafnode));
            return const_iterator(this, info.tail, last.cnt);
        }
        // Check whether this BTree is empty
        bool empty() const {    return info.size == 0;  }
        // Return the number of <K,V> pairs
        size_t size() const {   return info.size;   }
        // Clear the BTree
        void clear() {
            fp = fopen(Filename, "w");
            fclose(fp);
            fileopen();
            initree();
        }
        // Return the value refer to the Key(key)
        Value at(const Key& key){
            iterator it = find(key);
            leafnode leaf;
            if(it == end()) {
                throw "not found";
            }
            return  it.getValue();
        }
        /**
         * Returns the number of elements with key
         *   that compares equivalent to the specified argument,
         * The default method of check the equivalence is !(a < b || b > a)
         */
        size_t count(const Key& key) const {
            return static_cast <size_t> (find(key) != iterator(nullptr));
        }
        /**
         * Finds an element with key equivalent to key.
         * key value of the element to search for.
         * Iterator to an element with key equivalent to key.
         *   If no such element is found, past-the-end (see end()) iterator is
         * returned.
         */
        iterator find(const Key& key) {
            off_t leafpos = findpos(key, info.root);
            if(leafpos == 0) return end();
            leafnode leaf;
            fileread(&leaf, leafpos, 1, sizeof(leafnode));
            for (int i = 0; i < leaf.cnt; ++i)
                if (leaf.data[i].first == key) return iterator(this, leafpos, i);
            return end();
        }
        const_iterator find(const Key& key) const {
            off_t leafpos = findpos(key, info.root);
            if(leafpos == 0) return cend();
            leafnode leaf;
            fileread(&leaf, leafpos, 1, sizeof(leafnode));
            for (int i = 0; i < leaf.cnt; ++i)
                if (leaf.data[i].first == key) return const_iterator(this, leafpos, i);
            return cend();
        }
    };
}  // namespace sjtu
//Grateful acknowledgement is made to my tutors and classmates!!
