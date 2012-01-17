
#include <unistd.h>

#include <osmocom/core/talloc.h>
#include <osmocom/sim/sim.h>

struct osim_decoded_data *osim_file_decode(struct osim_file *file,
					   int len, uint8_t *data)
{
	struct osim_decoded_data *dd;

	if (!file->desc->ops.parse)
		return NULL;

	dd = talloc_zero(file, struct osim_decoded_data);
	dd->file = file;

	if (file->desc->ops.parse(dd, file->desc, len, data) < 0) {
		talloc_free(dd);
		return NULL;
	} else
		return dd;
}

struct msgb *osim_file_encode(const struct osim_file_desc *desc,
				const struct osim_decoded_data *data)
{
	if (!desc->ops.encode)
		return NULL;

	return desc->ops.encode(desc, data);
}


