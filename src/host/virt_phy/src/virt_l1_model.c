#include <virtphy/virt_l1_model.h>
#include <talloc.h>

struct l1_model_ms* l1_model_ms_init(void *ctx)
{
	struct l1_model_ms *model = talloc_zero(ctx, struct l1_model_ms);
	model->state = talloc_zero(ctx, struct l1_state_ms);
	return model;
}

void l1_model_ms_destroy(struct l1_model_ms *model)
{
	virt_um_destroy(model->vui);
	l1ctl_sock_destroy(model->lsi);
	talloc_free(model->state);
	talloc_free(model);
}
