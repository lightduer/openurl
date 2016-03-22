#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "sqlite3.h"
#include "mpse.h"
#include "memory.h"

/*
 * Struct define
 */
struct content_node
{
        struct content_node *next;

        char content[64];
        unsigned int content_len;

	unsigned int nocase;
        unsigned int offset;
        unsigned int depth;
};

struct policy_node
{
	struct policy_node *next;
	char name[64];

	struct content_node *content_list;
	unsigned int percent;
	unsigned int content_num;
	unsigned int hit_times;
};

struct priv_data
{
	struct priv_data *prev;

	struct policy_node *policy;
	struct content_node *content;

	int iid;
	int is_last;
};

/*
 * Functions
 */
static int dump_tree(struct policy_node *policy_tree)
{
	struct policy_node *pnode = NULL;
        struct content_node *cnode = NULL;

        pnode = policy_tree;
        while (pnode)
        {
		MPSE_DEBUG("keyword group name:%s, num:%d", pnode->name, pnode->content_num);
                cnode = pnode->content_list;
                while (cnode)
                {
			MPSE_DEBUG("keyword:%s, len:%d", cnode->content, cnode->content_len);
                        cnode = cnode->next;
                }

                pnode = pnode->next;
        }

	return 0;
}

static int get_keyword(sqlite3 *keyword_db, struct policy_node *pnode)
{
	struct content_node *cnode = NULL;
        struct content_node *parent_cnode = NULL;
        char *sql = NULL;
        int ret = 0;
        int row, col;
        char **result = NULL;
        int i;

        sql = sqlite3_mprintf("select keyword from keyword where group_name='%s'", pnode->name);
        if (sql == NULL)
        {
                return 1;
        }

        ret = sqlite3_get_table(keyword_db, sql, &result, &row, &col, NULL);
        if (ret != SQLITE_OK)
        {
                sqlite3_free(sql);
                return 1;
        }

        if (row == 0)
        {
                sqlite3_free_table(result);
                sqlite3_free(sql);
                return 1;
        }

        for (i = 1; i <= row; i++)
	{
		cnode = (struct content_node *)malloc(sizeof(struct content_node));
        	if (cnode == NULL)
		{
                	free(pnode);
			sqlite3_free_table(result);
			sqlite3_free(sql);
                	printf("alloc content node fail.\n");
                	return 1;
		}

		memset(cnode, 0, sizeof(struct content_node));
		cnode->next = NULL;
		strcpy(cnode->content, result[i*col + 0]);
		cnode->content_len = strlen(result[i*col + 0]);
		cnode->content[cnode->content_len + 1] = '\0';
		cnode->nocase = 0;
        	cnode->offset = 0;
        	cnode->depth = 0;

		if (pnode->content_list == NULL)
			pnode->content_list = cnode;
		else
			parent_cnode->next = cnode;
		parent_cnode = cnode;
	}

	pnode->content_num = i-1;

	sqlite3_free_table(result);
        sqlite3_free(sql);
	return 0;
}

static int create_policy_tree(struct policy_node **policy_tree)
{
	struct policy_node *pnode = NULL;
	struct policy_node *parent_pnode = NULL;
	sqlite3 *keyword_db = NULL;
        char *sql = NULL;
        int ret = 0;
        int row, col;
        char **result = NULL;
        int i;

	ret = sqlite3_open("./keyword.db", &keyword_db);
        if (ret != SQLITE_OK)
        {
                return 1;
        }

	sql = sqlite3_mprintf("select name, percent from keyword_group");
        if (sql == NULL)
        {
                sqlite3_close(keyword_db);
                return 1;
        }

	ret = sqlite3_get_table(keyword_db, sql, &result, &row, &col, NULL);
        if (ret != SQLITE_OK)
        {
                sqlite3_close(keyword_db);
                sqlite3_free(sql);
                return 1;
        }

	if (row == 0)
        {
                sqlite3_free_table(result);
                sqlite3_close(keyword_db);
                sqlite3_free(sql);
                return 1;
        }

	for (i = 1; i <= row; i++)
        {
		pnode = (struct policy_node *)malloc(sizeof(struct policy_node));
		if (pnode == NULL)
		{
			printf("alloc policy node fail.\n");
			return 1;
		}

		memset(pnode, 0, sizeof(struct policy_node));
		pnode->next = NULL;
		pnode->content_list = NULL;
		strcpy(pnode->name, result[i*col + 0]);
		pnode->percent = atoi(result[i*col + 1]);
		if (get_keyword(keyword_db, pnode))
			return 1;

		if (*policy_tree == NULL)
			*policy_tree = pnode;
		else
			parent_pnode->next = pnode;
		parent_pnode = pnode;
        }

	sqlite3_free_table(result);
        sqlite3_close(keyword_db);
        sqlite3_free(sql);

	return 0;
}

static void destory_policy_tree(struct policy_node *tree)
{
	struct policy_node *pnode = NULL;
	struct policy_node *free_pnode = NULL;
        struct content_node *cnode = NULL;
	struct content_node *free_cnode = NULL;

	pnode = tree;
	while (pnode)
	{
		cnode = pnode->content_list;
		while (cnode)
		{
			free_cnode = cnode;
			cnode = cnode->next;

			free(free_cnode);
			free_cnode = NULL;
		}

		pnode->content_list = NULL;
		free_pnode = pnode;
		pnode = pnode->next;

		free(free_pnode);
		free_pnode = NULL;
	}

	tree = NULL;
	return;
}

static int add_policy_tree_to_mpse(void *mpse, struct policy_node *policy, int iid)
{
	struct content_node *cnode = NULL;
	struct priv_data *priv = NULL;
	struct priv_data *last_priv = NULL;

	cnode = policy->content_list;
	while (cnode)
	{
		priv = (struct priv_data *)malloc(sizeof(struct priv_data));
		if (priv == NULL)
		{
			printf("alloc priv data fail.\n");
			return 1;
		}
		priv->prev = last_priv;
		priv->policy = policy;
		priv->content = cnode;
		priv->iid = iid;

		if (mpse_add_pattern(mpse, (unsigned char *)cnode->content, cnode->content_len,
			cnode->nocase, cnode->offset, cnode->depth,
			priv, iid)< 0)
		{
			free(priv);
			return 1;
		}

		last_priv = priv;
		iid++;

		cnode = cnode->next;
	}

	if (last_priv)
		last_priv->is_last = 1;

	return 0;
}

static int get_buf_from_file(char *buf, int *buf_len)
{
	int fd, n;

        memset(buf, 0, *buf_len);

        fd = open("./a", O_RDONLY);
        if (fd < 0)
                return 1;

        while ((n = read(fd, buf, *buf_len)) > 0) {}
        if (n < 0)
                printf("read file fail.\n");
        else
	{
		buf[strlen(buf)-1] = '\0';
                MPSE_DEBUG("%s", buf);
	}

	close(fd);
	return 0;
}

static int callback_func(void *priv_data, int index, int id, void *data)
{
	struct priv_data *priv = priv_data;
	struct policy_node *policy = priv->policy;
	int percent = 0;

	MPSE_DEBUG("match content:%s", priv->content->content);
	policy->hit_times++;
	MPSE_DEBUG("hit times:%d, content_num:%d", policy->hit_times, policy->content_num);

	percent = (policy->hit_times * 100) / policy->content_num;
	if (percent > policy->percent)
	{
		MPSE_DEBUG("match percent:%d\%", percent);
		policy->hit_times = 0;
		return 1;
	}

	return 0;
}

static int match_file(void *mpse)
{
	char buf[1024];
	int buf_len = 1024;
	int state = 0;
	int ret = 0;

	MPSE_DEBUG("match file start.");

	ret = get_buf_from_file(buf, &buf_len);
	if (ret)
	{
		printf("get buf from file fail.\n");
		return -1;
	}

	buf_len = strlen(buf);
	ret = mpse_search(mpse, (unsigned char *)buf, buf_len, callback_func, NULL, &state);
	if (ret > 0)
		return 1;

	return 0;
}

int main(int argc, char **argv)
{
	void *mpse_obj = NULL;
	struct policy_node *policy_tree = NULL;
	struct policy_node *pnode = NULL;
	int iid;
	int ret = 0;

	MPSE_DEBUG("-------------------------------start mpse test----------------------------------");
	mpse_obj = mpse_new("test", MPSE_AC, free);
	if (mpse_obj == NULL)
	{
		printf("create mpse object fail.\n");
		return 0;
	}

	ret = create_policy_tree(&policy_tree);
	if (ret)
	{
		printf("create policy tree fail.\n");
		goto out;
	}
	else
		dump_tree(policy_tree);
	
	pnode = policy_tree;
        while (pnode)
	{
		iid = mpse_pattern_count(mpse_obj) + 1;
		ret = add_policy_tree_to_mpse(mpse_obj, pnode, iid);
		if (ret)
		{
			printf("add policy tree to mpse fail.\n");
			goto out1;
		}

		pnode = pnode->next;
	}

	ret = mpse_compile(mpse_obj);
	if (ret < 0)
	{
		printf("compile mpse fail.\n");
		goto out1;
	}
	else
		mem_show();

	ret = match_file(mpse_obj);
	if (ret > 0)
		printf("match file.\n");
	else if (ret == 0)
		printf("not match file.\n");
	else
		printf("match fail.\n");

out1:
	destory_policy_tree(policy_tree);
out:
	mpse_free(mpse_obj);

	return 0;
}

