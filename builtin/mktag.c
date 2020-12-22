#include "builtin.h"
#include "tag.h"
#include "replace-object.h"
#include "object-store.h"
#include "fsck.h"
#include "config.h"

static struct fsck_options fsck_options = FSCK_OPTIONS_STRICT;

static int mktag_config(const char *var, const char *value, void *cb)
{
	return fsck_config_internal(var, value, cb, &fsck_options);
}

static int mktag_fsck_error_func(struct fsck_options *o,
				 const struct object_id *oid,
				 enum object_type object_type,
				 int msg_type, const char *message)
{
	switch (msg_type) {
	case FSCK_WARN:
	case FSCK_ERROR:
		/*
		 * We treat both warnings and errors as errors, things
		 * like missing "tagger" lines are "only" warnings
		 * under fsck, we've always considered them an error.
		 */
		fprintf_ln(stderr, "error: tag input does not pass fsck: %s", message);
		return 1;
	default:
		BUG("%d (FSCK_IGNORE?) should never trigger this callback",
		    msg_type);
	}
}

static int verify_object_in_tag(struct object_id *tagged_oid, int *tagged_type)
{
	int ret;
	enum object_type type;
	unsigned long size;
	void *buffer;
	const struct object_id *repl;

	buffer = read_object_file(tagged_oid, &type, &size);
	if (!buffer)
		die("could not read tagged object '%s'\n",
		    oid_to_hex(tagged_oid));
	if (type != *tagged_type)
		die("object '%s' tagged as '%s', but is a '%s' type\n",
		    oid_to_hex(tagged_oid),
		    type_name(*tagged_type), type_name(type));

	repl = lookup_replace_object(the_repository, tagged_oid);
	ret = check_object_signature(the_repository, repl,
				     buffer, size, type_name(*tagged_type));
	free(buffer);

	return ret;
}

int cmd_mktag(int argc, const char **argv, const char *prefix)
{
	struct strbuf buf = STRBUF_INIT;
	struct object_id tagged_oid;
	int tagged_type;
	struct object_id result;

	if (argc != 1)
		usage("git mktag");

	git_config(git_default_config, NULL);
	if (strbuf_read(&buf, 0, 0) < 0)
		die_errno("could not read from stdin");

	fsck_options.error_func = mktag_fsck_error_func;
	fsck_set_msg_type(&fsck_options, "extraheaderentry", "warn");
	/* config might set fsck.extraHeaderEntry=* again */
	git_config(mktag_config, NULL);
	if (fsck_tag_standalone(NULL, buf.buf, buf.len, &fsck_options,
				&tagged_oid, &tagged_type))
		die("tag on stdin did not pass our strict fsck check");

	if (verify_object_in_tag(&tagged_oid, &tagged_type))
		die("tag on stdin did not refer to a valid object");

	if (write_object_file(buf.buf, buf.len, tag_type, &result) < 0)
		die("unable to write tag file");

	strbuf_release(&buf);
	printf("%s\n", oid_to_hex(&result));
	return 0;
}
