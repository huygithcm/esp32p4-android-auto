/*
    Copyright 2026 Super VESC Display

    Reference dashboard theme — see theme_ref.c.
*/

#ifndef THEME_REF_H_
#define THEME_REF_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Register the reference theme with the dashboard-theme registry. Called from
 * custom_init_once(). */
void theme_ref_register(void);

#ifdef __cplusplus
}
#endif
#endif /* THEME_REF_H_ */
