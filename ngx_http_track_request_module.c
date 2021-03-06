
/*
 * Copyright (C) Maxim Dounin
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_str_t                 uri;
    ngx_array_t              *vars;
} ngx_http_track_request_conf_t;

typedef struct {
    ngx_uint_t                done;
    ngx_uint_t                status;
    ngx_http_request_t       *subrequest;
} ngx_http_track_request_ctx_t;


static ngx_int_t ngx_http_track_request_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_track_request_done(ngx_http_request_t *r,
    void *data, ngx_int_t rc);
static void *ngx_http_track_request_create_conf(ngx_conf_t *cf);
static char *ngx_http_track_request_merge_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_track_request_init(ngx_conf_t *cf);
static char *ngx_http_track_request(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_http_track_request_commands[] = {

    { ngx_string("track_request"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_track_request,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_track_request_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_track_request_init,            /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_track_request_create_conf,     /* create location configuration */
    ngx_http_track_request_merge_conf       /* merge location configuration */
};


ngx_module_t  ngx_http_track_request_module = {
    NGX_MODULE_V1,
    &ngx_http_track_request_module_ctx,     /* module context */
    ngx_http_track_request_commands,        /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_track_request_handler(ngx_http_request_t *r)
{
    ngx_http_request_t            *sr;
    ngx_http_post_subrequest_t    *ps;
    ngx_http_track_request_ctx_t   *ctx;
    ngx_http_track_request_conf_t  *arcf;

    arcf = ngx_http_get_module_loc_conf(r, ngx_http_track_request_module);

    if (arcf->uri.len == 0) {
        return NGX_DECLINED;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "track request handler");

    ctx = ngx_http_get_module_ctx(r, ngx_http_track_request_module);

    if (ctx != NULL) {
        if (!ctx->done) {
            return NGX_AGAIN;
        }

        /*
         * as soon as we are done - explicitly set variables to make
         * sure they will be available after internal redirects
         */

        /* return appropriate status */

        return NGX_OK;
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_track_request_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ps = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
    if (ps == NULL) {
        return NGX_ERROR;
    }

    ps->handler = ngx_http_track_request_done;
    ps->data = ctx;

    if (ngx_http_subrequest(r, &arcf->uri, NULL, &sr, ps,
                            NGX_HTTP_SUBREQUEST_WAITED)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    /*
     * allocate fake request body to avoid attempts to read it and to make
     * sure real body file (if already read) won't be closed by upstream
     */

    sr->request_body = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
    if (sr->request_body == NULL) {
        return NGX_ERROR;
    }

    sr->header_only = 1;

    ctx->subrequest = sr;

    ngx_http_set_ctx(r, ctx, ngx_http_track_request_module);

    return NGX_AGAIN;
}


static ngx_int_t
ngx_http_track_request_done(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
    ngx_http_track_request_ctx_t   *ctx = data;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "track request done s:%d", r->headers_out.status);

    ctx->done = 1;
    ctx->status = r->headers_out.status;

    return rc;
}


static void *
ngx_http_track_request_create_conf(ngx_conf_t *cf)
{
    ngx_http_track_request_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_track_request_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->uri.len = { 0, NULL };
     */

    conf->vars = NGX_CONF_UNSET_PTR;

    return conf;
}


static char *
ngx_http_track_request_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_track_request_conf_t *prev = parent;
    ngx_http_track_request_conf_t *conf = child;

    ngx_conf_merge_str_value(conf->uri, prev->uri, "");
    ngx_conf_merge_ptr_value(conf->vars, prev->vars, NULL);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_track_request_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_track_request_handler;

    return NGX_OK;
}


static char *
ngx_http_track_request(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_track_request_conf_t *arcf = conf;

    ngx_str_t        *value;

    if (arcf->uri.data != NULL) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        arcf->uri.len = 0;
        arcf->uri.data = (u_char *) "";

        return NGX_CONF_OK;
    }

    arcf->uri = value[1];

    return NGX_CONF_OK;
}
