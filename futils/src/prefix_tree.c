#include "prefix_tree.h"
#include "static_assert.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static const uint32_t FPTREE_SIGNATURE = 0x65657274u;   // This signature is used to check the prefix tree initialisation

enum
{
    FPTREE_DEF_KEY_LEN = 4
};

typedef struct fptree_leaf
{
    struct fptree_leaf *root;
    struct fptree_leaf *sibling;
    struct fptree_leaf *child;
    void               *data;
    uint32_t            key_len;
    uint8_t             key[FPTREE_DEF_KEY_LEN];
} fptree_leaf_t;

struct fptree
{
    uint32_t            signature;
    uint32_t            key_len;
    fptree_leaf_t       *root;
    fstatic_allocator_t *sallocator;
};

FSTATIC_ASSERT(sizeof(fptree_leaf_t) == FPTREE_LEAF_SIZE(FPTREE_DEF_KEY_LEN));
FSTATIC_ASSERT(sizeof(fptree_t) == FPTREE_HEADER_SIZE);

typedef enum fptree_iterator_type
{
    FPTREE_CHILDS_ITERATOR = 0,
    FPTREE_ROOTS_ITERATOR
} fptree_iterator_type_t;

struct fptree_iterator
{
    fptree_iterator_type_t type;
    fptree_t              *tree;
    uint32_t               size;
    uint32_t               top;
    fptree_leaf_t         *ptree_stack[1];
};

static int fptree_is_valid(fptree_t const *ptr)
{
    return ptr
           && ptr->signature == FPTREE_SIGNATURE
           && ptr->sallocator
           ? 1 : 0;
}

static uint32_t fptree_cmp_key(uint8_t const *lkey, uint32_t lkey_len, uint8_t const *rkey, uint32_t rkey_len)
{
    uint32_t i;
    uint32_t const len = lkey_len < rkey_len ? lkey_len : rkey_len;
    for(i = 0; i < len && lkey[i] == rkey[i]; ++i);
    return i;
}

static fptree_leaf_t *fptree_find_leaf(fptree_leaf_t *leaf, uint8_t const *key, uint32_t key_len, uint32_t *eq_total, uint32_t *eq_node, fptree_leaf_t **prev_sibling)
{
    fptree_leaf_t *p = leaf;
    *eq_node = *eq_total = 0;
    leaf = 0;

    if (prev_sibling) *prev_sibling = 0;

    while(p && key_len)
    {
        uint32_t len = fptree_cmp_key(p->key, p->key_len, key, key_len);

        if (len)
        {
            key += len;
            key_len -= len;
            *eq_total += len;
            *eq_node = len;

            leaf = p;

            if (!key_len || len < leaf->key_len)
                break;

            if (prev_sibling) *prev_sibling = 0;
            p = p->child;
        }
        else
        {
            if (prev_sibling) *prev_sibling = p;
            p = p->sibling;
        }
    }

    return leaf;
}

static void fptree_split_leaf(fptree_leaf_t *pleaf, fptree_leaf_t *new_leaf, uint32_t split_size)
{
    new_leaf->root = pleaf;
    new_leaf->sibling = 0;
    new_leaf->child = pleaf->child;
    new_leaf->data = pleaf->data;
    new_leaf->key_len = pleaf->key_len - split_size;
    memcpy(new_leaf->key, pleaf->key + split_size, new_leaf->key_len);

    pleaf->child = new_leaf;
    pleaf->data = 0;
    pleaf->key_len = split_size;
}

static fptree_leaf_t * fptree_join_child(fptree_leaf_t *p)
{
    fptree_leaf_t *del_node = 0;
    if (!p->data                        // no data
        && p->child
        && !p->child->sibling)          // node has only one child
    {
        del_node = p->child;
        memcpy(p->key + p->key_len, p->child->key, p->child->key_len);
        p->key_len += p->child->key_len;
        p->data = p->child->data;
        if (p->child->child)
            p->child->child->root = p;
        p->child = p->child->child;
    }
    return del_node;
}

static void fptree_add_child(fptree_leaf_t *pleaf, fptree_leaf_t *child, uint8_t const *key, uint32_t key_len, void *data)
{
    child->root = pleaf;
    child->sibling = pleaf->child;
    child->child = 0;
    child->data = data;
    child->key_len = key_len;
    memcpy(child->key, key, key_len);
    pleaf->child = child;
}

static void fptree_add_sibling(fptree_leaf_t *pleaf, fptree_leaf_t *sibling, uint8_t const *key, uint32_t key_len, void *data)
{
    sibling->root = pleaf->root;
    sibling->sibling = pleaf->sibling;
    sibling->child = 0;
    sibling->data = data;
    sibling->key_len = key_len;
    memcpy(sibling->key, key, key_len);
    pleaf->sibling = sibling;
}

static fptree_leaf_t *fptree_find_prev_sibling(fptree_leaf_t *root, fptree_leaf_t *p)
{
    if (p->root)
        root = p->root;
    for(fptree_leaf_t *l = root->child; l; l = l->sibling)
        if (p == l->sibling)
            return l;
    return 0;
}

ferr_t fptree_create(void *buf, uint32_t size, uint32_t key_len, fptree_t **pptree)
{
    fptree_t *ptr = (fptree_t*)buf;
    ferr_t err;
    if (!buf
        || size < sizeof(fptree_t)
        || !pptree
        || key_len > FPTREE_MAX_KEY_LEN)
        return FERR_INVALID_ARG;
    memset(ptr, 0, sizeof(fptree_t));
    ptr->signature = FPTREE_SIGNATURE;
    ptr->key_len = key_len;
    err = fstatic_allocator_create(ptr + 1, size - sizeof(fptree_t), FPTREE_LEAF_SIZE(key_len), &ptr->sallocator);
    *pptree = ptr;
    return err;
}

void fptree_delete(fptree_t *ptr)
{
    if (ptr)
    {
        fstatic_allocator_delete(ptr->sallocator);
        memset(ptr, 0, sizeof(fptree_t));
    }
}

ferr_t fptree_clear(fptree_t *ptr)
{
    if (!fptree_is_valid(ptr))
        return FERR_INVALID_ARG;
    ptr->root = 0;
    return fstatic_allocator_clear(ptr->sallocator);
}

static ferr_t fptree_node_insert_impl(fptree_t *ptr, uint8_t const *key, uint32_t key_len, void *data, int unique_insert, bool *is_unique)
{
    uint32_t eq_total, eq_node;
    fptree_leaf_t *p;
    fptree_leaf_t *new_leaf;

    if (!fptree_is_valid(ptr)
        || !key
        || key_len > ptr->key_len)
        return FERR_INVALID_ARG;

    if (is_unique)
        *is_unique = true;

    if (!ptr->root)
    {
        ptr->root = fstatic_alloc(ptr->sallocator);
        ptr->root->root = 0;
        ptr->root->sibling = 0;
        ptr->root->child = 0;
        ptr->root->data = data;
        ptr->root->key_len = key_len;
        memcpy(ptr->root->key, key, key_len);
        return FSUCCESS;
    }

    p = fptree_find_leaf(ptr->root, key, key_len, &eq_total, &eq_node, 0);

    assert(eq_total <= key_len);

    if (!eq_total)
    {
        new_leaf = fstatic_alloc(ptr->sallocator);
        if (!new_leaf) return FERR_NO_MEM;
        // insert sibling. Node isn't found.
        assert(!p && !eq_node);
        fptree_add_sibling(ptr->root, new_leaf, key, key_len, data);
        return FSUCCESS;
    }

    assert(p);
    if (!p) return FFAIL;

    if (eq_node == p->key_len)
    {
        // replace old data by new
        if (eq_total == key_len)
        {
            if (unique_insert && p->data)
                return FFAIL;
            if (is_unique)
                *is_unique = false;
            p->data = data;
            return FSUCCESS;
        }

        // add new node to childs list
        new_leaf = fstatic_alloc(ptr->sallocator);
        if (!new_leaf) return FERR_NO_MEM;
        fptree_add_child(p, new_leaf, key + eq_total, key_len - eq_total, data);
    }
    else if (eq_node && eq_node < p->key_len)
    {
        if (eq_total == key_len)
        {
            // split existing node and add new
            if (fstatic_allocator_available(ptr->sallocator) < 1)
                return FERR_NO_MEM;

            // split existing node
            new_leaf = fstatic_alloc(ptr->sallocator);
            if (!new_leaf) return FERR_NO_MEM;
            fptree_split_leaf(p, new_leaf, eq_node);

            p->data = data;
        }
        else
        {
            // split existing node and add new
            if (fstatic_allocator_available(ptr->sallocator) < 2)
                return FERR_NO_MEM;

            // split existing node
            new_leaf = fstatic_alloc(ptr->sallocator);
            if (!new_leaf) return FERR_NO_MEM;
            fptree_split_leaf(p, new_leaf, eq_node);

            // add new node
            new_leaf = fstatic_alloc(ptr->sallocator);
            if (!new_leaf) return FERR_NO_MEM;
            fptree_add_child(p, new_leaf, key + eq_total, key_len - eq_total, data);
        }
    }
    else
    {
        assert(0 && "Unexpected behaviour");
        return FFAIL;
    }

    return FSUCCESS;
}

ferr_t fptree_node_insert(fptree_t *ptr, uint8_t const *key, uint32_t key_len, void *data, bool *is_unique)
{
    return fptree_node_insert_impl(ptr, key, key_len, data, 0, is_unique);
}

ferr_t fptree_node_unique_insert(fptree_t *ptr, uint8_t const *key, uint32_t key_len, void *data)
{
    return fptree_node_insert_impl(ptr, key, key_len, data, 1, 0);
}

ferr_t fptree_node_delete(fptree_t *ptr, uint8_t const *key, uint32_t key_len)
{
    uint32_t       eq_total, eq_node;
    fptree_leaf_t *p;
    fptree_leaf_t *prev_sibling;

    if (!fptree_is_valid(ptr)
        || !key
        || key_len > ptr->key_len)
        return FERR_INVALID_ARG;

    p = fptree_find_leaf(ptr->root, key, key_len, &eq_total, &eq_node, &prev_sibling);

    if (!p || eq_total != key_len)
        return FFAIL;

    p->data = 0;

    if (!p->child)
    {
        if (prev_sibling)                       // has prev sibling
        {
            prev_sibling->sibling = p->sibling; // delete node from siblings list
            memset(p, 0, sizeof *p);
            fstatic_free(ptr->sallocator, p);
            p = prev_sibling->root;
        }
        else if (p->root)                       // has root
        {
            fptree_leaf_t *root = p->root;
            root->child = p->sibling;           // delete node from siblings list
            memset(p, 0, sizeof *p);
            fstatic_free(ptr->sallocator, p);
            p = root;
        }
        else
        {
            assert(p == ptr->root);             // deleted root node
            if (p != ptr->root)
                return FFAIL;
            ptr->root = ptr->root->sibling;
            memset(p, 0, sizeof *p);
            fstatic_free(ptr->sallocator, p);
            p = 0;
        }
    }

    while(p)
    {
        fptree_leaf_t *del_node = fptree_join_child(p);
        if (del_node)
        {
            memset(del_node, 0, sizeof *del_node);
            fstatic_free(ptr->sallocator, del_node);
            p = p->root;
        }
        else p = 0;
    }

    return FSUCCESS;
}

ferr_t fptree_node_find(fptree_t *ptr, uint8_t const *key, uint32_t key_len, void **pdata)
{
    fptree_leaf_t *leaf;
    uint32_t eq_total, eq_node;

    if (!fptree_is_valid(ptr)
        || !key
        || key_len > ptr->key_len)
        return FERR_INVALID_ARG;

    leaf = fptree_find_leaf(ptr->root, key, key_len, &eq_total, &eq_node, 0);

    if (!leaf || eq_total != key_len || eq_node != leaf->key_len)
    {
        if (pdata) *pdata = 0;
        return FFAIL;
    }

    if (pdata) *pdata = leaf->data;

    return FSUCCESS;
}

bool fptree_empty(fptree_t *ptr)
{
    if (fptree_is_valid(ptr))
        return ptr->root == 0;
    return true;
}

ferr_t fptree_iterator_create(fptree_t *ptr, fptree_iterator_t **iter)
{
    fptree_iterator_t *it;
    if (!fptree_is_valid(ptr)
        || !iter)
        return FERR_INVALID_ARG;
    *iter = 0;
    it = malloc(sizeof(fptree_iterator_t) + sizeof(fptree_leaf_t*) * fstatic_allocator_allocated(ptr->sallocator));
    if (!it) return FERR_NO_MEM;
    it->type = FPTREE_CHILDS_ITERATOR;
    it->tree = ptr;
    it->size = fstatic_allocator_allocated(ptr->sallocator);
    it->top = 0;
    *iter = it;
    return FSUCCESS;
}

void fptree_iterator_delete(fptree_iterator_t *iter)
{
    if (iter) free(iter);
}

static void fptree_get_node(fptree_iterator_t *iter, fptree_node_t *node)
{
    uint32_t i;
    node->key_len = 0;
    for (i = 0; i < iter->top; ++i)
    {
        memcpy(node->key + node->key_len, iter->ptree_stack[i]->key, iter->ptree_stack[i]->key_len);
        node->key_len += iter->ptree_stack[i]->key_len;
    }
    node->data = iter->ptree_stack[iter->top - 1]->data;
}

ferr_t fptree_first(fptree_iterator_t *iter, fptree_node_t *node)
{
    if (!iter || !node)
        return FERR_INVALID_ARG;
    if (!iter->tree->root)
        return FFAIL;

    if (iter->type == FPTREE_CHILDS_ITERATOR)
    {
        iter->ptree_stack[0] = iter->tree->root;
        iter->top = 1;
    }
    else
        iter->top = iter->size;

    fptree_get_node(iter, node);

    return FSUCCESS;
}

ferr_t fptree_next(fptree_iterator_t *iter, fptree_node_t *node)
{
    fptree_leaf_t *p;
    if (!iter || !node)
        return FERR_INVALID_ARG;
    if (!iter->top)
        return FFAIL;
    if (!iter->tree->root)
        return FFAIL;

    if (iter->type == FPTREE_CHILDS_ITERATOR)
    {
        p = iter->ptree_stack[iter->top - 1];

        if (p->child)
        {
            iter->ptree_stack[iter->top] = p->child;
            iter->top++;
        }
        else if (p->sibling)
            iter->ptree_stack[iter->top - 1] = p->sibling;
        else
        {
            while(!(p = iter->ptree_stack[iter->top - 1]->sibling))
            {
                iter->top--;
                if (!iter->top)
                    return FFAIL;
            }
            iter->ptree_stack[iter->top - 1] = p;
        }
    }
    else
    {
        iter->top--;
        if (!iter->top)
            return FFAIL;
    }

    fptree_get_node(iter, node);

    return FSUCCESS;
}

ferr_t fptree_iterator_node_delete(fptree_iterator_t *iter)
{
    if (!iter)
        return FERR_INVALID_ARG;
    if (!iter->top)
        return FFAIL;
    if (!iter->tree->root)
        return FFAIL;

    fptree_leaf_t *p = iter->ptree_stack[iter->top - 1];
    fptree_leaf_t *prev_sibling = fptree_find_prev_sibling(iter->tree->root, p);

    p->data = 0;

    if (!p->child)
    {
        if (prev_sibling)                       // has prev sibling
        {
            prev_sibling->sibling = p->sibling; // delete node from siblings list
            memset(p, 0, sizeof *p);
            fstatic_free(iter->tree->sallocator, p);
            p = prev_sibling->root;
        }
        else if (p->root)                       // has root
        {
            fptree_leaf_t *root = p->root;
            root->child = p->sibling;           // delete node from siblings list
            memset(p, 0, sizeof *p);
            fstatic_free(iter->tree->sallocator, p);
            p = root;
        }
        else
        {
            assert(p == iter->tree->root);      // deleted root node
            if (p != iter->tree->root)
                return FFAIL;
            iter->tree->root = iter->tree->root->sibling;
            memset(p, 0, sizeof *p);
            fstatic_free(iter->tree->sallocator, p);
            p = 0;
        }
        iter->top--;
    }

    // TODO
/*
    while(p)
    {
        fptree_leaf_t *del_node = fptree_join_child(p);
        if (del_node)
        {
            memset(del_node, 0, sizeof *del_node);
            fstatic_free(ptr->sallocator, del_node);
            p = p->root;
        }
        else p = 0;
    }
*/
    return FSUCCESS;
}
