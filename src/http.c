#include <net/http.h>
#include <json.h>
#include "pump.h"

static u8_t request_buffer[512];

static struct http_ctx http_ctx;
static struct http_server_urls http_urls;

static void send_response(struct http_ctx *ctx,
			  const struct sockaddr *dst,
			  char *status,
			  bool json,
			  char *body,
			  int bodylen)
{
	char header[64];
	char *content_type = "application/json";
	if (!json)
		content_type = "text/html";

	snprintf(header, sizeof header, HTTP_PROTOCOL " %s" HTTP_CRLF, status);
	http_add_header(ctx, header, dst, NULL);
	http_add_header_field(ctx, "Content-Type", content_type, dst, NULL);
	http_add_header_field(ctx, "Transfer-Encoding", "chunked", dst, NULL);

	snprintf(header, sizeof header, "%d", bodylen);
	http_add_header_field(ctx, "Content-Length", header, dst, NULL);
	http_add_header(ctx, HTTP_CRLF, dst, NULL);

	http_send_chunk(ctx, body, bodylen, dst, NULL);
	http_send_chunk(ctx, NULL, 0, dst, NULL);
	//http_add_header(ctx, HTTP_CRLF, dst, NULL);
	http_send_flush(ctx, NULL);
	http_close(ctx);
}


static void respond_not_found(struct http_ctx *ctx,
			      const struct sockaddr *dst)
{
	char body[] = "<body><html>Error 404 not found</html></body>";
	send_response(ctx, dst, "404 Not Found", false, body, sizeof body);
}

static void respond_error(struct http_ctx *ctx,
			  const struct sockaddr *dst,
			  char *string)
{
	char body[64];
	int bodylen = snprintf(body, sizeof body,
			       "<body><html>%s</html></body>", string);
	send_response(ctx, dst, "500 Error", false, body, bodylen);
}

static void pump_request(struct http_ctx *ctx,
			 const struct sockaddr *dst,
			 enum http_method method)
{
	char* body = strstr(ctx->http.url, "\r\n\r\n");
	int ret;

	if (method == HTTP_PUT) {
		if (body) {
			if (pump_consume_json(body, strlen(body)) < 0) {
				respond_error(ctx, dst, "consume json failed");
				return;
			}
		}
		else
			printk("PUT without body!\n%s\n", ctx->http.url);
	}

	char outbuf[128];
	ret = pump_produce_json(outbuf, sizeof outbuf);
	if (ret < 0) {
		respond_error(ctx, dst, "produce json failed");
		return;
	}
	send_response(ctx, dst, "200 OK", true, outbuf, strlen(outbuf));
}

static enum http_verdict url_handler(struct http_ctx *ctx,
				     enum http_connection_type type,
				     const struct sockaddr *dst)
{
#if 1 // debug output
	char url[128];
	int url_len = ctx->http.url_len;

	if (url_len >= sizeof url)
		url_len = sizeof url - 1;
	memcpy(url, ctx->http.url, url_len);
	url[url_len] = 0;

	printk("http in : %s %s\n", http_method_str(ctx->http.parser.method), url);
#endif

	if (!strncmp(ctx->http.url, "/v1/pumps/0", ctx->http.url_len)) {
		pump_request(ctx, dst, ctx->http.parser.method);
	}
	else
		respond_not_found(ctx, dst);
	return HTTP_VERDICT_DROP;
}

void http_start(void)
{
	struct sockaddr *server_addr;
	server_addr = NULL;

	http_server_add_default(&http_urls, url_handler);

	int ret = http_server_init(&http_ctx, &http_urls,
				   server_addr,
				   request_buffer, sizeof request_buffer,
				   NULL,
				   NULL);
	if (ret < 0) {
		printk("Failed to initialize HTTP server (%d)\n", ret);
		return;
	}

	http_set_cb(&http_ctx, NULL, NULL, NULL, NULL );

	http_server_enable(&http_ctx);
	printk("HTTP server started\n");
}
