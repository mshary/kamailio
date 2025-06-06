/*
 * Usrloc module interface
 *
 * Copyright (C) 2001-2003 FhG Fokus
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Kamailio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * Kamailio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/*! \file
 *  \brief USRLOC - Usrloc module interface
 *  \ingroup usrloc
 *
 * - Module \ref usrloc
 */

/*!
 * \defgroup usrloc Usrloc :: User location module
 * \brief User location module
 *
 * The module keeps a user location table and provides access
 * to the table to other modules. The module exports no functions
 * that could be used directly from scripts, all access is done
 * over an API. A main user of this API is the registrar module.
 * \see registrar
 */

#include <stdio.h>
#include "p_usrloc_mod.h"
#include "../../core/sr_module.h"
#include "../../core/dprint.h"
#include "../../core/rpc_lookup.h"
#include "../../core/timer_proc.h"
#include "../../core/globals.h"
#include "../../core/ut.h" /* str_init */
#include "udomain.h"	   /* {insert,delete,get,release}_urecord */
#include "urecord.h"	   /* {insert,delete,get}_ucontact */
#include "ucontact.h"	   /* update_ucontact */
#include "ul_mi.h"
#include "../usrloc/ul_callback.h"
#include "ul_db_api.h"
#include "ul_db_watch.h"
#include "ul_check.h"
#include "ul_db.h"
#include "ul_db_layer.h"
#include "dlist.h"
#include "config.h"

MODULE_VERSION

#define RUID_COL "ruid"
#define USER_COL "username"
#define DOMAIN_COL "domain"
#define CONTACT_COL "contact"
#define EXPIRES_COL "expires"
#define Q_COL "q"
#define CALLID_COL "callid"
#define CSEQ_COL "cseq"
#define FLAGS_COL "flags"
#define CFLAGS_COL "cflags"
#define USER_AGENT_COL "user_agent"
#define RECEIVED_COL "received"
#define PATH_COL "path"
#define SOCK_COL "socket"
#define METHODS_COL "methods"
#define INSTANCE_COL "instance"
#define REG_ID_COL "reg_id"
#define LAST_MOD_COL "last_modified"
#define UNIQ_COL "uniq"

static int mod_init(void);		 /*!< Module initialization function */
static void destroy(void);		 /*!< Module destroy function */
static int child_init(int rank); /*!< Per-child init function */
extern int bind_usrloc(usrloc_api_t *api);
extern int ul_locks_no;

#define UL_PRELOAD_SIZE 8
static char *ul_preload_list[UL_PRELOAD_SIZE];
static int ul_preload_index = 0;
static int ul_preload_param(modparam_t type, void *val);

/*
 * Module parameters and their default values
 */

/**
 * @var params
 * defines the parameters which can be set in the Kamailio config file
 * is stored. Only used when @see use_domain is set to 1
 * @param write_db_url Url to the database where the key and database information is
 * stored and where errors are reported to. Only used when @see write_on_db is active.
 * @param read_db_url Url to the database where the key and database information is
 * stored.
 * @param reg_db_table the name of the table containing the information about the
 * partitioned databases.
 * @param id_column name of the column containing the id mapping to a key.
 * @param num_column name of the column containing the number of the entry.
 * @param url_column name of the column containing the URL to the database.
 * @param status_column name of the column containing the status of the database.
 * (1=ON, 2=OFF)
 * @param failover_time_column name of the column containing the time when the
 * database's status changed or a spare has been activated.
 * @param spare_flag_column name of the column containing the information if an entry
 * works as spare for broken dbs (0=no spare, 1=spare)
 * @param error_column name of the column containing the errors which occurred on
 * the database.
 * @param risk_group_column name of the column containing the databases risk group
 * Only used when spare databases are used.
 * @param expire_time specifies the expire time of contacts
 * @param db_err_threshold specifies the amount of errors when at which a db
 * gets deactivated
 * @param failover_level defines if the module shall search for spares or just
 * turnoff a broken db
 * @param db_retry_interval defines in which intervals the module shall try to
 * reconnect to a deactivated database
 * @param write_on_db defines if the module has write access on the databases or not
 * @param alg_location defines the algorithm for the location matching - based on crc32 for  now
 */

str ruid_col =
		str_init(RUID_COL); /*!< Name of column containing record unique id */
str user_col = str_init(USER_COL); /*!< Name of column containing usernames */
str domain_col = str_init(DOMAIN_COL); /*!< Name of column containing domains */
str contact_col = str_init(
		CONTACT_COL); /*!< Name of column containing contact addresses */
str expires_col =
		str_init(EXPIRES_COL); /*!< Name of column containing expires values */
str q_col = str_init(Q_COL);   /*!< Name of column containing q values */
str callid_col =
		str_init(CALLID_COL); /*!< Name of column containing callid string */
str cseq_col = str_init(CSEQ_COL); /*!< Name of column containing cseq values */
str flags_col =
		str_init(FLAGS_COL); /*!< Name of column containing internal flags */
str cflags_col = str_init(CFLAGS_COL);
str user_agent_col = str_init(
		USER_AGENT_COL); /*!< Name of column containing user agent string */
str received_col = str_init(
		RECEIVED_COL); /*!< Name of column containing transport info of REGISTER */
str path_col =
		str_init(PATH_COL); /*!< Name of column containing the Path header */
str sock_col = str_init(
		SOCK_COL); /*!< Name of column containing the received socket */
str methods_col = str_init(
		METHODS_COL); /*!< Name of column containing the supported methods */
str instance_col = str_init(
		INSTANCE_COL); /*!< Name of column containing the SIP instance value */
str reg_id_col =
		str_init(REG_ID_COL); /*!< Name of column containing the reg-id value */
str last_mod_col = str_init(
		LAST_MOD_COL); /*!< Name of column containing the last modified date */
str uniq_col =
		str_init(UNIQ_COL); /*!< Name of column containing the uniq value*/
int db_mode =
		3; /*!< Database sync scheme:  1-write through, 2-write back, 3-only db */
int use_domain = 0; /*!< Whether usrloc should use domain part of aor */
int use_domain_crc32 =
		1; /*!< Whether usrloc should use domain part of aor when calculating crc32 */
int desc_time_order = 0; /*!< By default do not enable timestamp ordering */

int ul_fetch_rows = 2000; /*!< number of rows to fetch from result */
int ul_hash_size = 9;
str write_db_url = str_init(DEFAULT_DB_URL);
str read_db_url = str_init(DEFAULT_DB_URL);
str reg_table = str_init(REG_TABLE);
str id_col = str_init(ID_COL);
str url_col = str_init(URL_COL);
str num_col = str_init(NUM_COL);
str status_col = str_init(STATUS_COL);
str failover_time_col = str_init(FAILOVER_T_COL);
str spare_col = str_init(SPARE_COL);
str error_col = str_init(ERROR_COL);
str risk_group_col = str_init(RISK_GROUP_COL);
int retry_interval = DB_RETRY;
int policy = DB_DEFAULT_POLICY;
int db_write = 0;
int db_master_write = 0;
int alg_location = 0;
int mdb_availability_control = 0;

int db_use_transactions = 0;
str db_transaction_level = str_init(DB_DEFAULT_TRANSACTION_LEVEL);
char *isolation_level;
int connection_expires = DB_DEFAULT_CONNECTION_EXPIRES;
int max_loc_nr = 0;


/* flags */
unsigned int nat_bflag = (unsigned int)-1;
unsigned int init_flag = 0;

str default_db_url = str_init(DEFAULT_DB_URL);
str default_db_type = str_init(DEFAULT_DB_TYPE);
str domain_db = str_init(DEFAULT_DOMAIN_DB);
int default_dbt = 0;
int expire = 0;
int *mdb_w_available;

/* clang-format off */
/*! \brief
 * Exported functions
 */
static cmd_export_t cmds[] = {
	{"ul_bind_usrloc", (cmd_function)bind_usrloc, 1, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};

/*! \brief
 * Exported parameters
 */
static param_export_t params[] = {
	{"ruid_column", PARAM_STR, &ruid_col},
	{"user_column", PARAM_STR, &user_col},
	{"domain_column", PARAM_STR, &domain_col},
	{"contact_column", PARAM_STR, &contact_col},
	{"expires_column", PARAM_STR, &expires_col},
	{"q_column", PARAM_STR, &q_col},
	{"callid_column", PARAM_STR, &callid_col},
	{"cseq_column", PARAM_STR, &cseq_col},
	{"flags_column", PARAM_STR, &flags_col},
	{"cflags_column", PARAM_STR, &cflags_col},
	{"db_mode", PARAM_INT, &db_mode},
	{"use_domain", PARAM_INT, &use_domain},
	{"use_domain_crc32", PARAM_INT, &use_domain_crc32},
	{"desc_time_order", PARAM_INT, &desc_time_order},
	{"user_agent_column", PARAM_STR, &user_agent_col},
	{"received_column", PARAM_STR, &received_col},
	{"path_column", PARAM_STR, &path_col},
	{"socket_column", PARAM_STR, &sock_col},
	{"methods_column", PARAM_STR, &methods_col},
	{"matching_mode", PARAM_INT, &default_p_usrloc_cfg.matching_mode},
	{"cseq_delay", PARAM_INT, &cseq_delay},
	{"fetch_rows", PARAM_INT, &ul_fetch_rows},
	{"hash_size", PARAM_INT, &ul_hash_size},
	{"nat_bflag", PARAM_INT, &nat_bflag},
	{"default_db_url", PARAM_STR, &default_db_url},
	{"default_db_type", PARAM_STR, &default_db_type},
	{"domain_db", PARAM_STR, &domain_db},
	{"instance_column", PARAM_STR, &instance_col},
	{"reg_id_column", PARAM_STR, &reg_id_col},
	{"write_db_url", PARAM_STR, &write_db_url},
	{"read_db_url", PARAM_STR, &read_db_url},
	{"reg_db_table", PARAM_STR, &reg_table},
	{"id_column", PARAM_STR, &id_col},
	{"num_column", PARAM_STR, &num_col},
	{"url_column", PARAM_STR, &url_col},
	{"status_column", PARAM_STR, &status_col},
	{"failover_time_column", PARAM_STR, &failover_time_col},
	{"spare_flag_column", PARAM_STR, &spare_col},
	{"error_column", PARAM_STR, &error_col},
	{"risk_group_column", PARAM_STR, &risk_group_col},
	{"expire_time", PARAM_INT, &default_p_usrloc_cfg.expire_time},
	{"UTC_timestamps", PARAM_INT, &default_p_usrloc_cfg.utc_timestamps},
	{"db_err_threshold", PARAM_INT, &default_p_usrloc_cfg.db_err_threshold},
	{"failover_level", PARAM_INT, &default_p_usrloc_cfg.failover_level},
	{"db_retry_interval", PARAM_INT, &retry_interval},
	{"db_use_transactions", PARAM_INT, &db_use_transactions},
	{"db_transaction_level", PARAM_INT, &db_transaction_level},
	{"write_on_db", PARAM_INT, &db_write},
	{"write_on_master_db", PARAM_INT, &db_master_write},
	{"connection_expires", PARAM_INT, &connection_expires},
	{"alg_location", PARAM_INT, &alg_location},
	{"db_ops_ruid", PARAM_INT, &default_p_usrloc_cfg.db_ops_ruid},
	{"db_update_as_insert", PARAM_INT, &default_p_usrloc_cfg.db_update_as_insert},
	{"mdb_availability_control", PARAM_INT, &mdb_availability_control},
	{"preload", PARAM_STRING | PARAM_USE_FUNC, (void *)ul_preload_param},
	{0, 0, 0}
};

stat_export_t mod_stats[] = {
	{"registered_users", STAT_IS_FUNC, (stat_var **)get_number_of_users},
	{0, 0, 0}
};

struct module_exports exports = {
	"p_usrloc",      /*!< module name  */
	DEFAULT_DLFLAGS, /* dlopen flags */
	cmds,            /* exported functions */
	params,          /* exported parameters */
	0,               /* RPC method exports */
	0,               /* exported pseudo-variables */
	0,               /* response handling function */
	mod_init,        /* module initialization function */
	child_init,      /* per-child init function */
	destroy          /* module destroy function */
};
/* clang-format on */


/*! \brief
 * Module initialization function
 */
static int mod_init(void)
{
	int i = 0;
	udomain_t *d;
	int matching_mode_cfg = cfg_get(p_usrloc, p_usrloc_cfg, matching_mode);

#ifdef STATISTICS
	/* register statistics */
	if(register_module_stats("p_usrloc", mod_stats) != 0) {
		LM_ERR("failed to register core statistics\n");
		return -1;
	}
#endif

	if(ul_hash_size <= 1)
		ul_hash_size = 512;
	else
		ul_hash_size = 1 << ul_hash_size;
	ul_locks_no = ul_hash_size;

	if(cfg_declare("p_usrloc", p_usrloc_cfg_def, &default_p_usrloc_cfg,
			   cfg_sizeof(p_usrloc), &p_usrloc_cfg)) {
		LM_ERR("Fail to declare the configuration\n");
		return -1;
	}
	/* check matching mode */
	switch(matching_mode_cfg) {
		case CONTACT_ONLY:
		case CONTACT_CALLID:
		case CONTACT_PATH:
			break;
		default:
			LM_ERR("invalid matching mode %d\n", matching_mode_cfg);
	}

	if(ul_init_locks() != 0) {
		LM_ERR("locks array initialization failed\n");
		return -1;
	}

	/* init the callbacks list */
	if(init_ulcb_list() < 0) {
		LM_ERR("usrloc/callbacks initialization failed\n");
		return -1;
	}

	/* preload tables */
	for(i = 0; i < ul_preload_index; i++) {
		if(register_udomain((const char *)ul_preload_list[i], &d) < 0) {
			LM_ERR("cannot register preloaded table %s\n", ul_preload_list[i]);
			return -1;
		}
	}

	if(db_mode != DB_ONLY) {
		LM_ERR("DB_ONLY is the only mode possible for partitioned usrloc. "
			   "Please set db_mode to 3\n");
		return -1;
	}

	/* Shall we use database ? */
	if(db_mode != NO_DB) { /* Yes */
		if(!default_db_url.s || default_db_url.len <= 0) {
			LM_ERR("must set default_db_url parameter\n");
			return -1;
		}

		if(strcmp(DB_TYPE_CLUSTER_STR, default_db_type.s) == 0) {
			default_dbt = DB_TYPE_CLUSTER;
		} else {
			default_dbt = DB_TYPE_SINGLE;
		}

		if(ul_db_layer_init() < 0) {
			return -1;
		}

		if(ul_fetch_rows <= 0) {
			LM_ERR("invalid fetch_rows number '%d'\n", ul_fetch_rows);
			return -1;
		}
	}

	if(nat_bflag == (unsigned int)-1) {
		nat_bflag = 0;
	} else if(nat_bflag >= 8 * sizeof(nat_bflag)) {
		LM_ERR("bflag index (%d) too big!\n", nat_bflag);
		return -1;
	} else {
		nat_bflag = 1 << nat_bflag;
	}

	init_flag = 1;

	if((isolation_level = pkg_malloc(strlen("SET TRANSACTION ISOLATION LEVEL ")
									 + db_transaction_level.len + 1))
			== NULL) {
		LM_ERR("couldn't allocate private memory.\n");
		return -1;
	}
	sprintf(isolation_level, "SET TRANSACTION ISOLATION LEVEL %s",
			db_transaction_level.s);
	if(init_list() < 0) {
		LM_ERR("could not init check list.\n");
		return -1;
	}
	if(ul_db_init() < 0) {
		LM_ERR("could not initialise databases.\n");
		return -1;
	}
	if(ul_db_watch_init() < 0) {
		LM_ERR("could not init database watch environment.\n");
		return -1;
	}

	if((mdb_w_available = shm_malloc(sizeof(int))) == NULL) {
		LM_ERR("couldn't allocate shared memory. \n");
		return -1;
	}
	if(db_master_write) {
		/* register extra dummy timer to be created in init_db_check() */
		register_dummy_timers(1);
		if(mdb_availability_control) {
			check_master_db();
		}
	}
	return 0;
}


static int child_init(int _rank)
{
	if(_rank == PROC_INIT) {
		if(init_db_check() < 0) {
			LM_ERR("could not initialise database check.\n");
			return -1;
		}
		return 0;
	}
	if(ul_db_child_init() < 0) {
		LM_ERR("could not initialise databases.\n");
		return -1;
	}


	return 0;
}


/*! \brief
 * Module destroy function
 */
static void destroy(void)
{
	/* we need to sync DB in order to flush the cache */
	ul_unlock_locks();

	free_all_udomains();
	ul_destroy_locks();
	/* free callbacks list */
	destroy_ulcb_list();

	ul_db_shutdown();
	destroy_list();
	ul_db_watch_destroy();
}


#ifdef MI_REMOVED
static int mi_child_loc_nr_init(void)
{
	if(ul_db_child_locnr_init() < 0) {
		LM_ERR("could not retrieve location number from database. Try to "
			   "reinitialize the db handles\n");
		return -1;
	}
	return 0;
}


struct mi_root *mi_ul_db_refresh(struct mi_root *cmd_tree, void *param)
{
	int ret;
	ret = set_must_refresh();

	LM_INFO("sp-ul_db location databases were refreshed (%i elements).\n", ret);

	return init_mi_tree(200, MI_OK_S, MI_OK_LEN);
}


struct mi_root *mi_loc_nr_refresh(struct mi_root *cmd_tree, void *param)
{
	/* this function does nothing, all work is done per each child in the mi_child_loc_nr_init function */
	return init_mi_tree(200, MI_OK_S, MI_OK_LEN);
}
#endif

time_t ul_db_datetime_set(time_t v)
{
	if(cfg_get(p_usrloc, p_usrloc_cfg, utc_timestamps) == 1) {
		return local2utc(v);
	} else {
		return v;
	}
}

time_t ul_db_datetime_get(time_t v)
{
	if(cfg_get(p_usrloc, p_usrloc_cfg, utc_timestamps) == 1) {
		return utc2local(v);
	} else {
		return v;
	}
}

/*! \brief
 * preload module parameter handler
 */
static int ul_preload_param(modparam_t type, void *val)
{
	if(val == NULL) {
		LM_ERR("invalid parameter\n");
		goto error;
	}
	if(ul_preload_index >= UL_PRELOAD_SIZE) {
		LM_ERR("too many preloaded tables\n");
		goto error;
	}
	ul_preload_list[ul_preload_index] = (char *)val;
	ul_preload_index++;
	return 0;
error:
	return -1;
}
