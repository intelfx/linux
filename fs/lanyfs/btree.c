/*
 * btree.c - Lanyard Filesystem Binary Tree Operations
 *
 * Copyright (C) 2012  Dan Luedtke <mail@danrl.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "lanyfs_km.h"

/**
 * lanyfs_btree_any_link() - Returns left or right link of an inode.
 * @inode:			inode
 *
 * Left link will be preferred, returns 0 if no link is found.
 */
static lanyfs_blk_t lanyfs_btree_any_link(struct inode *inode)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(inode);
	if (lii->left)
		return lii->left;
	else
		return lii->right;
}

/**
 * lanyfs_btree_set_left() - Sets left link of inode to given address.
 * @inode:			inode
 * @addr:			new target address of link
 */
static void lanyfs_btree_set_left(struct inode *inode, lanyfs_blk_t addr)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(inode);
	spin_lock(&lii->lock);
	lii->left = addr;
	spin_unlock(&lii->lock);
	mark_inode_dirty(inode);
}

/**
 * lanyfs_btree_set_right() - Sets right link of inode to given address.
 * @inode:			inode
 * @addr:			new target address of link
 */
static void lanyfs_btree_set_right(struct inode *inode, lanyfs_blk_t addr)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(inode);
	spin_lock(&lii->lock);
	lii->right = addr;
	spin_unlock(&lii->lock);
	mark_inode_dirty(inode);
}

/**
 * lanyfs_btree_rpl_link() - Replaces a link of an inode.
 * @inode:			inode
 * @old:			link to be replaced
 * @new:			replacement
 *
 * Only one link will be replaced even if both links match @old. Left link
 * is always preferred.
 */
static void lanyfs_btree_rpl_link(struct inode *inode, lanyfs_blk_t old,
				   lanyfs_blk_t new)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(inode);
	if (lii->left == old) {
		spin_lock(&lii->lock);
		lii->left = new;
		spin_unlock(&lii->lock);
	} else if (lii->right == old) {
		spin_lock(&lii->lock);
		lii->right = new;
		spin_unlock(&lii->lock);
	}
	mark_inode_dirty(inode);
}

/**
 * lanyfs_btree_set_subtree() - Sets subtree of a directory.
 * @dir:			directory to modify
 * @addr:			new address for subtree
 */
static void lanyfs_btree_set_subtree(struct inode *dir, lanyfs_blk_t addr)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(dir);
	spin_lock(&lii->lock);
	lii->subtree = addr;
	spin_unlock(&lii->lock);
	mark_inode_dirty(dir);
}

/**
 * __lanyfs_btree_add_inode() - Adds an inode to a binary tree.
 * @cur:			current inode
 * @rookie:			inode to be added
 *
 * This function is internal and is best be called by its wrapper function.
 */
static int __lanyfs_btree_add_inode(struct inode *cur, struct inode *rookie)
{
	struct lanyfs_lii *lii_cur;
	struct lanyfs_lii *lii_rki;
	struct inode *tmp;
	int cmp;
	int ret;
	lanyfs_debug_function(__FILE__, __func__);

	lii_cur = LANYFS_I(cur);
	lii_rki = LANYFS_I(rookie);
	cmp = strncmp(lii_cur->name, lii_rki->name, LANYFS_NAME_LENGTH);

	/* insert node or dig deeper if necessary */
	ret = 0;
	if (cmp < 0) {
		if (lii_cur->left) {
			tmp = lanyfs_iget(cur->i_sb, lii_cur->left);
			if (!tmp)
				return -EINVAL;
			ret = __lanyfs_btree_add_inode(tmp, rookie);
			iput(tmp);
		} else {
			lanyfs_btree_set_left(cur, rookie->i_ino);
		}
	} else if (cmp > 0) {
		if (lii_cur->right) {
			tmp = lanyfs_iget(cur->i_sb, lii_cur->right);
			if (!tmp)
				return -EINVAL;
			ret = __lanyfs_btree_add_inode(tmp, rookie);
			iput(tmp);
		} else {
			lanyfs_btree_set_right(cur, rookie->i_ino);
		}
	} else {
		ret = -EEXIST;
	}
	return ret;
}

/**
 * lanyfs_btree_add_inode() - Adds an inode to a binary tree.
 * @dir:			directory to insert inode into
 * @rookie:			inode to be added
 */
int lanyfs_btree_add_inode(struct inode *dir, struct inode *rookie)
{
	struct lanyfs_lii *lii;
	struct inode *root;
	int ret;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(dir);
	ret = 0;
	if (lii->subtree) {
		ret = -EINVAL;
		root = lanyfs_iget(dir->i_sb, lii->subtree);
		if (root) {
			ret = __lanyfs_btree_add_inode(root, rookie);
			iput(root);
		}
	} else {
		lanyfs_btree_set_subtree(dir, rookie->i_ino);
	}
	return ret;
}

/**
 * __lanyfs_btree_del_inode() - Removes an inode from a binary tree.
 * @dir:			directory containing the tree
 * @par:			parent inode of @cur in tree
 * @cur:			node to be tested for liquidation
 * @name:			directory or file name of inode to be removed
 *
 * This function is internal and is best be called by its wrapper function.
 */
static int __lanyfs_btree_del_inode(struct inode *dir, struct inode *par,
				    struct inode *cur, const char *name)
{
	struct inode *lm; /* left-most inode of right subtree */
	struct inode *lmp; /* left-most's parent */
	struct inode *tmp;
	struct lanyfs_lii *lii_cur;
	struct lanyfs_lii *lii_lm;
	int cmp;
	int ret;
	lanyfs_debug_function(__FILE__, __func__);

	lm = lmp = tmp = NULL;
	lii_cur = LANYFS_I(cur);
	cmp = strncmp(lii_cur->name, name, LANYFS_NAME_LENGTH);
	if (cmp < 0 && lii_cur->left) {
		/* object we are looking for must be in left subtree */
		tmp = lanyfs_iget(dir->i_sb, lii_cur->left);
		if (tmp) {
			ret = __lanyfs_btree_del_inode(dir, cur, tmp, name);
			iput(tmp);
			return ret;
		}
	} else if (cmp > 0 && lii_cur->right) {
		/* object we are looking for must be in right subtree */
		tmp = lanyfs_iget(dir->i_sb, lii_cur->right);
		if (tmp) {
			ret = __lanyfs_btree_del_inode(dir, cur, tmp, name);
			iput(tmp);
			return ret;
		}
	} else if (cmp == 0 && !lii_cur->left && !lii_cur->right) {
		/* case I: node is a leaf */
		if (par) {
			/* inode has parent */
			lanyfs_btree_rpl_link(par, cur->i_ino, 0);
		} else {
			/* last inode in tree just died */
			lanyfs_btree_set_subtree(dir, 0);
		}
	} else if (cmp == 0 && lii_cur->left && lii_cur->right) {
		/* case II: node has two subtrees */

		/* find leftmost in right subtree of p */
		lmp = cur;
		lm = lanyfs_iget(dir->i_sb, lii_cur->right);
		if (!lm)
			goto exit_clean;
		lii_lm = LANYFS_I(lm);
		while (lii_lm->left) {
			if (lmp != cur)
				iput(lmp);
			lmp = lm;
			lm = lanyfs_iget(dir->i_sb, lii_lm->left);
			lii_lm = LANYFS_I(lm);
		}

		if (lmp != cur) {
			/* get left-most's child */
			tmp = lanyfs_iget(dir->i_sb, lanyfs_btree_any_link(lm));
			if (!tmp)
				goto exit_clean;
			lanyfs_btree_rpl_link(lmp, lm->i_ino, tmp->i_ino);
			lanyfs_btree_set_right(lm, lii_cur->right);
			iput(tmp);
		}
		lanyfs_btree_set_left(lm, lii_cur->left);

		if (par)
			lanyfs_btree_rpl_link(par, cur->i_ino, lm->i_ino);
		else
			lanyfs_btree_set_subtree(dir, lm->i_ino);
		iput(lm);
	} else if (cmp == 0 && (lii_cur->left || lii_cur->right)) {
		/* case III: node has one subtree */
		tmp = lanyfs_iget(dir->i_sb, lanyfs_btree_any_link(cur));
		if (!tmp)
			goto exit_clean;
		if (par)
			lanyfs_btree_rpl_link(par, cur->i_ino, tmp->i_ino);
		else
			lanyfs_btree_set_subtree(dir, tmp->i_ino);
		iput(tmp);
	} else {
		return -ENOENT;
	}
	return 0;
exit_clean:
	if (tmp)
		iput(tmp);
	if (lmp && lmp != cur)
		iput(lmp);
	if (lm)
		iput(lm);
	return -ENOENT;
}

/**
 * lanyfs_btree_del_inode() - Removes an inode from a binary tree.
 * @dir:			directory containing victim inode
 * @name:			directory or file name of inode to be removed
 *
 * Deleting a node from a binary tree often leads to resorting the tree.
 * Sometimes the root node changes, and this is why we have @dir as argument.
 * It will automatically be updated by this function to ensure proper directory
 * listings and overall consistency.
 */
int lanyfs_btree_del_inode(struct inode *dir, const char *name)
{
	struct lanyfs_lii *lii;
	struct inode *root;
	int ret;
	lanyfs_debug_function(__FILE__, __func__);

	ret = -ENOENT;
	lii = LANYFS_I(dir);
	if (lii->subtree) {
		root = lanyfs_iget(dir->i_sb, lii->subtree);
		if (root) {
			ret = __lanyfs_btree_del_inode(dir, NULL, root, name);
			iput(root);
		} else {
			ret = -ENOMEM;
		}
	}
	return ret;
}

/**
 * __lanyfs_btree_lookup() - Looks up a inode in a binary tree by name.
 * @cur:			current inode in binary search tree
 * @name:			name to look up
 *
 * This function is internal and is best be called by its wrapper function.
 */
static struct inode *__lanyfs_btree_lookup(struct inode *cur, const char *name)
{
	struct lanyfs_lii *lii;
	struct inode *next;
	struct inode *ret;
	int cmp;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(cur);
	cmp = strncmp(lii->name, name, LANYFS_NAME_LENGTH);
	ret = NULL;
	if (cmp < 0 && lii->left) {
		/* left tree */
		next = lanyfs_iget(cur->i_sb, lii->left);
		if (next) {
			ret =  __lanyfs_btree_lookup(next, name);
			if (ret != next)
				iput(next);
		}
	} else if (cmp > 0 && lii->right) {
		/* right tree */
		next = lanyfs_iget(cur->i_sb, lii->right);
		if (next) {
			ret = __lanyfs_btree_lookup(next, name);
			if (ret != next)
				iput(next);
		}
	} else if (cmp == 0) {
		/* we found it */
		return cur;
	}
	return ret;
}

/**
 * lanyfs_btree_lookup() - Looks up an inode in a directory by name.
 * @dir:			directory to look into
 * @name:			name to look up
 *
 * Returns inode with increased reference count.
 */
struct inode *lanyfs_btree_lookup(struct inode *dir, const char *name)
{
	struct lanyfs_lii *lii;
	struct inode *root;
	struct inode *ret;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(dir);
	if (lii->subtree) {
		root = lanyfs_iget(dir->i_sb, lii->subtree);
		if (root) {
			ret = __lanyfs_btree_lookup(root, name);
			if (ret != root)
				iput(root);
			return ret;
		}
	}
	return NULL;
}

/**
 * lanyfs_btree_clear_inode() - Sets all links of an inode to 0.
 * @inode:			inode to clear
 */
void lanyfs_btree_clear_inode(struct inode *inode)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);
	lii = LANYFS_I(inode);
	spin_lock(&lii->lock);
	lii->left = 0;
	lii->right = 0;
	spin_unlock(&lii->lock);
	mark_inode_dirty(inode);
}
