/*
 * Copyright (c) 2014, 2015 Mikey Austin <mikey@greyd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file   sqlite.c
 * @brief  SQLite DB driver.
 * @author Mikey Austin
 * @date   2014
 */

#include <config.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_SQLITE3_H
#  include <sqlite3.h>
#endif

#include "../src/failures.h"
#include "../src/greydb.h"

#define DEFAULT_PATH "/var/db/greyd"
#define DEFAULT_DB   "greyd.sqlite"

/**
 * The internal driver handle.
 */
struct s3_handle {
    sqlite3 *db;
    int txn;
};

struct s3_itr {
    sqlite3_stmt *stmt;
    struct DB_key *curr;
};

static void populate_key(sqlite3_stmt *, struct DB_key *, int);
static void populate_val(sqlite3_stmt *, struct DB_val *, int);

extern void
Mod_db_init(DB_handle_T handle)
{
    struct s3_handle *dbh;
    char *path;
    int ret, flags, uid_changed = 0;

    path = Config_get_str(handle->config, "path", "database", DEFAULT_PATH);
    if(mkdir(path, 0700) == -1) {
        if(errno != EEXIST)
            i_critical("sqlite3 db path: %s", strerror(errno));
    }
    else {
        /*
         * As the directory has just been created, ensure the correct
         * ownership.
         */
        if(handle->pw
           && (chown(path, handle->pw->pw_uid, handle->pw->pw_gid) == -1))
        {
            i_critical("chown %s failed: %s", path, strerror(errno));
        }
    }

    if((dbh = malloc(sizeof(*dbh))) == NULL)
        i_critical("malloc: %s", strerror(errno));
    dbh->db = NULL;
    dbh->txn = 0;
    handle->dbh = dbh;
}

extern void
Mod_db_open(DB_handle_T handle, int flags)
{
    struct s3_handle *dbh = handle->dbh;
    char *db_name, *path, *db_path = NULL;
    int ret;

    if(dbh->db != NULL)
        return;

    path = Config_get_str(handle->config, "path", "database", DEFAULT_PATH);
    db_name = Config_get_str(handle->config, "db_name", "database",
                             DEFAULT_DB);

    if(asprintf(&db_path, "%s/%s", path, db_name) <= 0) {
        i_warning("could not create db path");
        goto cleanup;
    }

    ret = sqlite3_open(db_path, &dbh->db);
    if(ret != SQLITE_OK) {
        i_warning("could not open %s: %s", db_path, sqlite3_errstr(ret));
        goto cleanup;
    }

    free(db_path);
    return;

cleanup:
    exit(1);
}

extern int
Mod_db_start_txn(DB_handle_T handle)
{
    struct s3_handle *dbh = handle->dbh;
    char *err;
    int ret;

    if(dbh->txn != 0) {
        /* Already in a transaction. */
        return -1;
    }

    ret = sqlite3_exec(dbh->db, "begin", NULL, NULL, &err);
    if(ret != SQLITE_OK) {
        i_warning("db txn start failed: %s", err);
        sqlite3_free(err);
        goto cleanup;
    }
    dbh->txn = 1;

    return ret;

cleanup:
    if(dbh->db)
        sqlite3_close(dbh->db);
    exit(1);
}

extern int
Mod_db_commit_txn(DB_handle_T handle)
{
    struct s3_handle *dbh = handle->dbh;
    char *err;
    int ret;

    if(dbh->txn != 1) {
        i_warning("cannot commit, not in transaction");
        return -1;
    }

    ret = sqlite3_exec(dbh->db, "commit", NULL, NULL, &err);
    if(ret != SQLITE_OK) {
        i_warning("db txn commit failed: %s", err);
        sqlite3_free(err);
        goto cleanup;
    }
    dbh->txn = 0;

    return ret;

cleanup:
    if(dbh->db)
        sqlite3_close(dbh->db);
    exit(1);
}

extern int
Mod_db_rollback_txn(DB_handle_T handle)
{
    struct s3_handle *dbh = handle->dbh;
    char *err;
    int ret;

    if(dbh->txn != 1) {
        return -1;
    }

    ret = sqlite3_exec(dbh->db, "rollback", NULL, NULL, &err);
    if(ret != SQLITE_OK) {
        i_warning("db txn rollback failed: %s", err);
        sqlite3_free(err);
        goto cleanup;
    }
    dbh->txn = 0;

    return ret;

cleanup:
    if(dbh->db)
        sqlite3_close(dbh->db);
    exit(1);
}

extern void
Mod_db_close(DB_handle_T handle)
{
    struct s3_handle *dbh;

    if((dbh = handle->dbh) != NULL) {
        if(dbh->db)
            sqlite3_close(dbh->db);
        free(dbh);
        handle->dbh = NULL;
    }
}

extern int
Mod_db_put(DB_handle_T handle, struct DB_key *key, struct DB_val *val)
{
    struct s3_handle *dbh = handle->dbh;
    sqlite3_stmt *stmt;
    char *sql;
    struct Grey_tuple *gt;
    struct Grey_data *gd;
    int ret;

    switch(key->type) {
    case DB_KEY_MAIL:
        sql = "INSERT OR IGNORE INTO spamtraps(address) VALUES (?)";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        ret = sqlite3_bind_text(stmt, 1, key->data.s, -1, SQLITE_STATIC);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_bind_text: %s", sqlite3_errstr(ret));
            goto err;
        }
        break;

    case DB_KEY_IP:
        sql = "INSERT INTO entries "
            "(`ip`, `first`, `pass`, `expire`, `bcount`, `pcount`) "
            "VALUES (?, ?, ?, ?, ?, ?)";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        gd = &val->data.gd;
        if(!(!sqlite3_bind_text(stmt,     1, key->data.s, -1, SQLITE_STATIC)
             && !sqlite3_bind_int64(stmt, 2, gd->first)
             && !sqlite3_bind_int64(stmt, 3, gd->pass)
             && !sqlite3_bind_int64(stmt, 4, gd->expire)
             && !sqlite3_bind_int(stmt,   5, gd->bcount)
             && !sqlite3_bind_int(stmt,   6, gd->pcount)))
        {
            i_warning("sqlite3_bind_*: %s", sqlite3_errmsg(dbh->db));
            goto err;
        }
        break;

    case DB_KEY_TUPLE:
        sql = "INSERT INTO entries "
            "(`ip`, `helo`, `from`, `to`, "
            " `first`, `pass`, `expire`, `bcount`, `pcount`) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql), &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        gt = &key->data.gt;
        gd = &val->data.gd;
        if(!(!sqlite3_bind_text(stmt,     1, gt->ip, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  2, gt->helo, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  3, gt->from, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  4, gt->to, -1, SQLITE_STATIC)
             && !sqlite3_bind_int64(stmt, 5, gd->first)
             && !sqlite3_bind_int64(stmt, 6, gd->pass)
             && !sqlite3_bind_int64(stmt, 7, gd->expire)
             && !sqlite3_bind_int(stmt,   8, gd->bcount)
             && !sqlite3_bind_int(stmt,   9, gd->pcount)))
        {
            i_warning("sqlite3_bind_*: %s", sqlite3_errmsg(dbh->db));
            goto err;
        }
        break;

    default:
        return GREYDB_ERR;
    }

    ret = sqlite3_step(stmt);
    if(ret != SQLITE_DONE) {
        i_warning("unexpected sqlite3_step result: %d", ret);
        sqlite3_finalize(stmt);
        return GREYDB_ERR;
    }
    sqlite3_finalize(stmt);
    return GREYDB_OK;

err:
    sqlite3_finalize(stmt);
    DB_rollback_txn(handle);
    return GREYDB_ERR;
}

extern int
Mod_db_get(DB_handle_T handle, struct DB_key *key, struct DB_val *val)
{
    struct s3_handle *dbh = handle->dbh;
    sqlite3_stmt *stmt;
    char *sql;
    struct Grey_tuple *gt;
    int ret, res;

    switch(key->type) {
    case DB_KEY_MAIL:
        sql = "SELECT 0, 0, 0, 0, -2 "
            "FROM spamtraps WHERE `address`=? "
            "LIMIT 1";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        ret = sqlite3_bind_text(stmt, 1, key->data.s, -1, SQLITE_STATIC);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_bind_text: %s", sqlite3_errstr(ret));
            goto err;
        }
        break;

    case DB_KEY_IP:
        sql = "SELECT `first`, `pass`, `expire`, `bcount`, `pcount`"
            "FROM entries "
            "WHERE `ip`=? AND `helo` IS NULL AND `from` IS NULL "
            "AND `to` IS NULL "
            "LIMIT 1";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        if(sqlite3_bind_text(stmt, 1, key->data.s, -1, SQLITE_STATIC)
            != SQLITE_OK)
        {
            i_warning("sqlite3_bind_*: %s", sqlite3_errmsg(dbh->db));
            goto err;
        }
        break;

    case DB_KEY_TUPLE:
        sql = "SELECT `first`, `pass`, `expire`, `bcount`, `pcount`"
            "FROM entries "
            "WHERE `ip`=? AND `helo`=? AND `from`=? AND `to`=? "
            "LIMIT 1";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        gt = &key->data.gt;
        if(!(!sqlite3_bind_text(stmt,     1, gt->ip, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  2, gt->helo, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  3, gt->from, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  4, gt->to, -1, SQLITE_STATIC)))
        {
            i_warning("sqlite3_bind_*: %s", sqlite3_errmsg(dbh->db));
            goto err;
        }
        break;

    default:
        return GREYDB_ERR;
    }

    ret = sqlite3_step(stmt);
    switch(ret) {
    case SQLITE_DONE:
        /* Not found as there are no rows returned. */
        res = GREYDB_NOT_FOUND;
        break;

    case SQLITE_ROW:
        /* We have a result. */
        res = GREYDB_FOUND;
        populate_val(stmt, val, 0);
        break;

    default:
        i_warning("unexpected sqlite3_step result: %d", ret);
        res = GREYDB_ERR;
        break;
    }

err:
    sqlite3_finalize(stmt);
    return res;
}

extern int
Mod_db_del(DB_handle_T handle, struct DB_key *key)
{
    struct s3_handle *dbh = handle->dbh;
    struct Grey_tuple *gt;
    sqlite3_stmt *stmt;
    char *sql;
    int ret;

    switch(key->type) {
    case DB_KEY_MAIL:
        sql = "DELETE FROM spamtraps WHERE `address`=?";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        ret = sqlite3_bind_text(stmt, 1, key->data.s, -1, SQLITE_STATIC);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_bind_text: %s", sqlite3_errstr(ret));
            goto err;
        }
        break;

    case DB_KEY_IP:
        sql = "DELETE FROM entries WHERE `ip`=? "
            "AND `helo` IS NULL AND `from` IS NULL AND `to` IS NULL";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        ret = sqlite3_bind_text(stmt, 1, key->data.s, -1, SQLITE_STATIC);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_bind_text: %s", sqlite3_errstr(ret));
            goto err;
        }
        break;

    case DB_KEY_TUPLE:
        sql = "DELETE FROM entries WHERE `ip`=? "
            "AND `helo`=? AND `from`=? AND `to`=?";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        gt = &key->data.gt;
        if(!(!sqlite3_bind_text(stmt,     1, gt->ip, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  2, gt->helo, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  3, gt->from, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  4, gt->to, -1, SQLITE_STATIC)))
        {
            i_warning("sqlite3_bind_*: %s", sqlite3_errmsg(dbh->db));
            goto err;
        }
        break;

    default:
        return GREYDB_ERR;
    }

    ret = sqlite3_step(stmt);
    if(ret != SQLITE_DONE) {
        i_warning("unexpected sqlite3_step result: %d", ret);
        sqlite3_finalize(stmt);
        return GREYDB_ERR;
    }
    sqlite3_finalize(stmt);
    return GREYDB_OK;

err:
    sqlite3_finalize(stmt);
    DB_rollback_txn(handle);
    return GREYDB_ERR;
}

extern void
Mod_db_get_itr(DB_itr_T itr)
{
    struct s3_handle *dbh = itr->handle->dbh;
    struct s3_itr *dbi = NULL;
    char *sql;
    int ret;

    if((dbi = malloc(sizeof(*dbi))) == NULL) {
        i_warning("malloc: %s", strerror(errno));
        goto err;
    }
    dbi->stmt = NULL;
    dbi->curr = NULL;

    sql = "SELECT `ip`, `helo`, `from`, `to`, "
        "`first`, `pass`, `expire`, `bcount`, `pcount` FROM entries";
    ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &dbi->stmt, NULL);
    if(ret != SQLITE_OK) {
        i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
        goto err;
    }

err:
    DB_rollback_txn(itr->handle);
    sqlite3_close(dbh->db);
    exit(1);
}

extern void
Mod_db_itr_close(DB_itr_T itr)
{
    struct s3_handle *dbh = itr->handle->dbh;
    struct s3_itr *dbi = itr->dbi;

    if(dbi) {
        dbi->curr = NULL;
        if(dbi->stmt)
            sqlite3_finalize(dbi->stmt);
        free(dbi);
        itr->dbi = NULL;
    }
}

extern int
Mod_db_itr_next(DB_itr_T itr, struct DB_key *key, struct DB_val *val)
{
    struct s3_itr *dbi = itr->dbi;
    int ret, res;

    ret = sqlite3_step(dbi->stmt);
    switch(ret) {
    case SQLITE_DONE:
        /* We have reached the end as there are no rows. */
        res = GREYDB_NOT_FOUND;
        break;

    case SQLITE_ROW:
        /* We have a result. */
        res = GREYDB_FOUND;
        populate_key(dbi->stmt, key, 0);
        populate_val(dbi->stmt, val, 4);
        dbi->curr = key;
        break;

    default:
        i_warning("unexpected sqlite3_step result: %d", ret);
        res = GREYDB_ERR;
        break;
    }

    return res;
}

extern int
Mod_db_itr_replace_curr(DB_itr_T itr, struct DB_val *val)
{
    struct s3_handle *dbh = itr->handle->dbh;
    struct s3_itr *dbi = itr->dbi;
    struct DB_key *key = dbi->curr;
    sqlite3_stmt *stmt;
    char *sql;
    struct Grey_tuple *gt;
    struct Grey_data *gd;
    int ret;

    switch(key->type) {
    case DB_KEY_IP:
        sql = "UPDATE entries SET `first`=?, `pass`=?, `expire`=? "
            "`bcount`=?, `pcount`=? "
            "WHERE `ip`=? AND `helo` IS NULL AND `from` IS NULL "
            "AND `to` IS NULL";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        gt = &key->data.gt;
        gd = &val->data.gd;
        if(!(!sqlite3_bind_int64(stmt,    1, gd->first)
             && !sqlite3_bind_int64(stmt, 2, gd->pass)
             && !sqlite3_bind_int64(stmt, 3, gd->expire)
             && !sqlite3_bind_int(stmt,   4, gd->bcount)
             && !sqlite3_bind_int(stmt,   5, gd->pcount)
             && !sqlite3_bind_text(stmt,  6, gt->ip, -1, SQLITE_STATIC)))
        {
            i_warning("sqlite3_bind_*: %s", sqlite3_errmsg(dbh->db));
            goto err;
        }
        break;

    case DB_KEY_TUPLE:
        sql = "UPDATE entries SET `first`=?, `pass`=?, `expire`=? "
            "`bcount`=?, `pcount`=? "
            "WHERE `ip`=? AND `helo`=? AND `from`=? AND `to`=?";
        ret = sqlite3_prepare(dbh->db, sql, strlen(sql) + 1, &stmt, NULL);
        if(ret != SQLITE_OK) {
            i_warning("sqlite3_prepare: %s", sqlite3_errstr(ret));
            goto err;
        }

        gt = &key->data.gt;
        gd = &val->data.gd;
        if(!(!sqlite3_bind_int64(stmt,    1, gd->first)
             && !sqlite3_bind_int64(stmt, 2, gd->pass)
             && !sqlite3_bind_int64(stmt, 3, gd->expire)
             && !sqlite3_bind_int(stmt,   4, gd->bcount)
             && !sqlite3_bind_int(stmt,   5, gd->pcount)
             && !sqlite3_bind_text(stmt,  6, gt->ip, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  7, gt->helo, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  8, gt->from, -1, SQLITE_STATIC)
             && !sqlite3_bind_text(stmt,  9, gt->to, -1, SQLITE_STATIC)))
        {
            i_warning("sqlite3_bind_*: %s", sqlite3_errmsg(dbh->db));
            goto err;
        }
        break;

    default:
        return GREYDB_ERR;
    }

    ret = sqlite3_step(stmt);
    if(ret != SQLITE_DONE) {
        i_warning("unexpected sqlite3_step result: %d", ret);
        sqlite3_finalize(stmt);
        return GREYDB_ERR;
    }
    sqlite3_finalize(stmt);
    return GREYDB_OK;

err:
    sqlite3_finalize(stmt);
    DB_rollback_txn(itr->handle);
    return GREYDB_ERR;
}

extern int
Mod_db_itr_del_curr(DB_itr_T itr)
{
    struct s3_itr *dbi = itr->dbi;

    if(dbi && dbi->stmt && dbi->curr)
        return DB_del(itr->handle, dbi->curr);

    return GREYDB_ERR;
}

static void
populate_key(sqlite3_stmt *stmt, struct DB_key *key, int from)
{
    struct Grey_tuple *gt;
    static char buf[(INET6_ADDRSTRLEN + 1) + 4 * (GREY_MAX_MAIL + 1)];
    char *buf_p = buf;

    memset(key, 0, sizeof(*key));
    memset(buf, 0, sizeof(buf));

    /* A NULL helo column indicates an IP key type. */
    key->type = (sqlite3_column_type(stmt, 1) == SQLITE_NULL
                 ? DB_KEY_IP : DB_KEY_TUPLE);
    if(key->type == DB_KEY_IP) {
        key->data.s = buf_p;
        strncpy(buf_p, sqlite3_column_text(stmt, from + 0),
                INET6_ADDRSTRLEN + 1);
    }
    else {
        gt = &key->data.gt;
        gt->ip = buf_p;
        strncpy(buf_p, sqlite3_column_text(stmt, from + 0),
                INET6_ADDRSTRLEN + 1);
        buf_p += INET6_ADDRSTRLEN + 1;

        gt->helo = buf_p;
        strncpy(buf_p, sqlite3_column_text(stmt, from + 1),
                GREY_MAX_MAIL + 1);
        buf_p += GREY_MAX_MAIL + 1;

        gt->from = buf_p;
        strncpy(buf_p, sqlite3_column_text(stmt, from + 2),
                GREY_MAX_MAIL + 1);
        buf_p += GREY_MAX_MAIL + 1;

        gt->to = buf_p;
        strncpy(buf_p, sqlite3_column_text(stmt, from + 3),
                GREY_MAX_MAIL + 1);
        buf_p += GREY_MAX_MAIL + 1;

    }
}

static void
populate_val(sqlite3_stmt *stmt, struct DB_val *val, int from)
{
    struct Grey_data *gd;

    memset(val, 0, sizeof(*val));
    val->type = DB_VAL_GREY;
    gd = &val->data.gd;
    gd->first = sqlite3_column_int64(stmt,  from + 0);
    gd->pass = sqlite3_column_int64(stmt,   from + 1);
    gd->expire = sqlite3_column_int64(stmt, from + 2);
    gd->bcount = sqlite3_column_int(stmt,   from + 3);
    gd->pcount = sqlite3_column_int(stmt,   from + 4);
}