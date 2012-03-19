#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <thread>

extern "C" {
#include <libwebsockets.h>
#include <zlib.h>
}

#include <minisat/core/Dimacs.h>
#include <minisat/core/Solver.h>

static char *pre_padding;
static char *post_padding;

static int callback_http(libwebsocket_context *context, libwebsocket *wsi, libwebsocket_callback_reasons reason, void *user, void *in, size_t len)
{
	if (reason == LWS_CALLBACK_FILTER_NETWORK_CONNECTION)
		return 0;
	if (reason != LWS_CALLBACK_HTTP)
		return -1;

	fprintf(stderr, "Request for file %s (%lu)\n", (char *) in, len);

	if (!strcmp((const char *) in, "/")) {
		if (libwebsockets_serve_http_file(wsi, "www/index.html", "text/html")) {
			fprintf(stderr, "Failed to send index.html\n");
			return -1;
		}

		return 0;
	}

	static const struct {
		const char *path;
		const char *mime_type;
	} whitelist[] = {
		{ "/index.html", "text/html" },
		{ "/default.css", "text/css" },

		{ "/favicon.ico", "image/x-icon" },

		{ "/jquery-1.7.1.js", "application/javascript" },
		{ "/dygraph-combined.js", "application/javascript" },
		{ "/minisat.js", "application/javascript" },
	};

	static const unsigned int whitelist_n = sizeof(whitelist) / sizeof(*whitelist);

	for (unsigned int i = 0; i < whitelist_n; ++i) {
		if (strcmp((const char *) in, whitelist[i].path))
			continue;

		std::string filename;
		filename += "www";
		filename += whitelist[i].path;
		if (libwebsockets_serve_http_file(wsi, filename.c_str(), whitelist[i].mime_type)) {
			fprintf(stderr, "Failed to send %s\n", filename.c_str());
			return -1;
		}

		return 0;
	}

	/* XXX: Send 404 or something */
	return -1;
}

class WebSolver:
	public Minisat::Solver
{
public:
	bool playing;

	WebSolver():
		playing(false)
	{
	}

	~WebSolver()
	{
	}

	int trail_size()
	{
		return trail.size();
	}

	bool step()
	{
		using namespace Minisat;

		CRef confl;
		while (true) {
			confl = propagate();
			if (confl != CRef_Undef)
				break;

			Lit next = lit_Undef;
			while (decisionLevel() < assumptions.size()) {
				Lit p = assumptions[decisionLevel()];
				if (value(p) == l_True) {
					newDecisionLevel();
				} else if (value(p) == l_False) {
					analyzeFinal(~p, conflict);
					return false;
				} else {
					next = p;
					break;
				}
			}

			if (next == lit_Undef) {
				next = pickBranchLit();
				if (next == lit_Undef)
					return false;
			}

			newDecisionLevel();
			uncheckedEnqueue(next);
		}

		if (decisionLevel() == 0)
			return false;

		vec<Lit> learnt_clause;
		int backtrack_level;

		analyze(confl, learnt_clause, backtrack_level);
		cancelUntil(backtrack_level);

		if (learnt_clause.size() == 1) {
			uncheckedEnqueue(learnt_clause[0]);
		} else {
			CRef cr = ca.alloc(learnt_clause, true);
			learnts.push(cr);
			attachClause(cr);
			claBumpActivity(ca[cr]);
			uncheckedEnqueue(learnt_clause[0], cr);
		}

		varDecayActivity();
		claDecayActivity();
		return true;
	}

	void backtrack()
	{
		cancelUntil(0);
	}
};

static WebSolver *solver;

static void restart(const libwebsocket_protocols *protocol)
{
	delete solver;
	solver = new WebSolver();

	gzFile in = gzopen("sha1-21.cnf", "rb");
	Minisat::parse_DIMACS(in, *solver);
	gzclose(in);

	if (!solver->simplify()) {
		/* XXX */
		printf("Unsat\n");
		exit(0);
	}

	char *str;
	int len = asprintf(&str, "%s{ action: 'restart' }%s",
		pre_padding, post_padding);
	assert(len != -1);

	int n = libwebsockets_broadcast(protocol, (unsigned char *) str + LWS_SEND_BUFFER_PRE_PADDING,
		len - LWS_SEND_BUFFER_PRE_PADDING - LWS_SEND_BUFFER_POST_PADDING);
	if (n < 0) {
		fprintf(stderr, "Broadcast failed\n");
		return;
	}

	/* XXX */
	free(str);
}

static void play(const libwebsocket_protocols *protocol)
{
	char *str;
	int len = asprintf(&str, "%s{ \"action\": \"play\" }%s",
		pre_padding, post_padding);
	assert(len != -1);

	int n = libwebsockets_broadcast(protocol, (unsigned char *) str + LWS_SEND_BUFFER_PRE_PADDING,
		len - LWS_SEND_BUFFER_PRE_PADDING - LWS_SEND_BUFFER_POST_PADDING);
	if (n < 0) {
		fprintf(stderr, "Broadcast failed\n");
		return;
	}

	/* XXX */
	free(str);
}

static void pause(const libwebsocket_protocols *protocol)
{
	char *str;
	int len = asprintf(&str, "%s{ \"action\": \"pause\" }%s",
		pre_padding, post_padding);
	assert(len != -1);

	int n = libwebsockets_broadcast(protocol, (unsigned char *) str + LWS_SEND_BUFFER_PRE_PADDING,
		len - LWS_SEND_BUFFER_PRE_PADDING - LWS_SEND_BUFFER_POST_PADDING);
	if (n < 0) {
		fprintf(stderr, "Broadcast failed\n");
		return;
	}

	/* XXX */
	free(str);
}

static void step(const libwebsocket_protocols *protocol)
{
	int trail_size = solver->trail_size();

	char *str;
	int len = asprintf(&str, "%s{ \"action\": \"step\", \"data\": %u }%s",
		pre_padding, trail_size, post_padding);
	assert(len != -1);

	int n = libwebsockets_broadcast(protocol, (unsigned char *) str + LWS_SEND_BUFFER_PRE_PADDING,
		len - LWS_SEND_BUFFER_PRE_PADDING - LWS_SEND_BUFFER_POST_PADDING);
	if (n < 0) {
		fprintf(stderr, "Broadcast failed\n");
		return;
	}

	/* XXX */
	free(str);
}

static int callback_minisat(libwebsocket_context *context, libwebsocket *wsi, libwebsocket_callback_reasons reason, void *user, void *in, size_t len)
{
	//printf("minisat callback, reason = %u\n", reason);

	if (reason == LWS_CALLBACK_ESTABLISHED) {
		return 0;
	}

	if (reason == LWS_CALLBACK_BROADCAST) {
		int n = libwebsocket_write(wsi, (unsigned char *) in, len, LWS_WRITE_TEXT);
		if (n < 0) {
			fprintf(stderr, "Write failed\n");
			return -1;
		}

		return 0;
	}

	if (reason == LWS_CALLBACK_RECEIVE) {
		//printf("got data: <%.*s> (%lu)\n", (int) len, (char *) in, len);

		if (!strncmp((const char *) in, "restart", len)) {
			restart(libwebsockets_get_protocol(wsi));
		}

		if (!strncmp((const char *) in, "play", len)) {
			solver->playing = true;
			play(libwebsockets_get_protocol(wsi));
		}

		if (!strncmp((const char *) in, "pause", len)) {
			solver->playing = false;
			pause(libwebsockets_get_protocol(wsi));
		}

		if (!strncmp((const char *) in, "step", len)) {
			if (solver->step()) {
				step(libwebsockets_get_protocol(wsi));
				solver->backtrack();
			}
		}

		return 0;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	pre_padding = new char[LWS_SEND_BUFFER_PRE_PADDING + 1];
	memset(pre_padding, 'A', LWS_SEND_BUFFER_PRE_PADDING);
	pre_padding[LWS_SEND_BUFFER_PRE_PADDING] = '\0';

	post_padding = new char[LWS_SEND_BUFFER_POST_PADDING + 1];
	memset(post_padding, 'A', LWS_SEND_BUFFER_POST_PADDING);
	post_padding[LWS_SEND_BUFFER_POST_PADDING] = '\0';

	libwebsocket_protocols protocols[] = {
		{ "http-only", callback_http, 0 },
		{ "minisat", callback_minisat, 0 },
		{ 0, 0, 0 },
	};

	libwebsocket_context *context = libwebsocket_create_context(8000, 0, protocols, libwebsocket_internal_extensions, 0, 0, -1, -1, 0);

	restart(&protocols[1]);

	while (true) {
		if (solver->playing) {
			if (solver->step()) {
				step(&protocols[1]);
				solver->backtrack();
			} else {
				restart(&protocols[1]);
			}
		}

		libwebsocket_service(context, 50);
	}

	return 0;
}
