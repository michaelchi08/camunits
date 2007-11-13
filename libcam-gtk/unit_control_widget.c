#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include <libcam/dbg.h>
#include "unit_control_widget.h"

#define err(args...) fprintf(stderr, args)

static void cam_unit_control_widget_finalize( GObject *obj );

static void on_output_formats_changed( CamUnit *unit, 
        CamUnitControlWidget *self );
static void on_close_button_clicked(GtkButton *bt, CamUnitControlWidget *self);
static void on_expander_notify( GtkWidget*widget, GParamSpec *param, 
        CamUnitControlWidget *self );
static void on_status_changed( CamUnit *unit, int old_status,
        CamUnitControlWidget *self );
static void on_control_value_changed(CamUnit *unit, CamUnitControl *ctl, 
        CamUnitControlWidget *self);
static void on_formats_combo_changed( GtkComboBox *combo, 
        CamUnitControlWidget *self );

enum {
    CLOSE_BUTTON_CLICKED_SIGNAL,
    LAST_SIGNAL
};

static guint unit_control_widget_signals[LAST_SIGNAL] = { 0 };

GtkTargetEntry cam_unit_control_widget_target_entry = {
    .target = "UnitControlWidget",
    .flags = GTK_TARGET_SAME_APP,
    .info = CAM_UNIT_CONTROL_WIDGET_DND_ID
};

typedef struct _ControlWidgetInfo {
    GtkWidget *widget;
    GtkWidget *file_chooser_bt;
    GtkWidget *labelval;
    GtkWidget *label;
    GtkWidget *button;
    int maxchars;
    CamUnitControl *ctl;
    int use_int;
} ControlWidgetInfo;

G_DEFINE_TYPE (CamUnitControlWidget, cam_unit_control_widget, 
        GTK_TYPE_EVENT_BOX);

static void
cam_unit_control_widget_init( CamUnitControlWidget *self )
{
    dbg(DBG_GUI, "unit control widget constructor\n");
    
    gtk_drag_source_set ( GTK_WIDGET(self), GDK_BUTTON1_MASK,
            &cam_unit_control_widget_target_entry, 1, GDK_ACTION_PRIVATE);

    // vbox for everything
    GtkWidget * vbox_outer = gtk_vbox_new (FALSE, 0);
    gtk_container_add (GTK_CONTAINER (self), vbox_outer);
    gtk_widget_show (vbox_outer);

    // frame for controls everything
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
    gtk_box_pack_start (GTK_BOX (vbox_outer), frame, FALSE, FALSE, 0);
    gtk_widget_show(frame);

    // vbox for rows
    GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add( GTK_CONTAINER(frame), vbox );
    gtk_widget_show(vbox);

    // box for expander and close button
    GtkWidget * hbox = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show (hbox);

    // alignment widget to contain the expander
    self->alignment = GTK_ALIGNMENT(gtk_alignment_new( 0, 0.5, 1, 0 ));
    gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET(self->alignment), 
            FALSE, FALSE, 0);
    gtk_widget_show( GTK_WIDGET(self->alignment) );

    // expander
    self->expander = GTK_EXPANDER(gtk_expander_new( NULL ));
    g_signal_connect(G_OBJECT(self->expander), "notify::expanded",
            G_CALLBACK(on_expander_notify), self);
    gtk_container_add(GTK_CONTAINER(self->alignment), 
            GTK_WIDGET(self->expander));
    //gtk_expander_set_expanded( self->expander, FALSE );
    gtk_widget_show(GTK_WIDGET(self->expander));

    self->exp_label = gtk_label_new ("blah");
    gtk_misc_set_alignment (GTK_MISC (self->exp_label), 0, 0.5);
    gtk_label_set_ellipsize (GTK_LABEL (self->exp_label), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (self->exp_label),
            TRUE, TRUE, 0);
    gtk_widget_show (GTK_WIDGET (self->exp_label));
    
    // close button
    self->close_button = GTK_BUTTON( gtk_button_new () );
    GtkWidget * image = gtk_image_new_from_stock (GTK_STOCK_CLOSE,
            GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image (self->close_button, image);
    gtk_button_set_relief (self->close_button, GTK_RELIEF_NONE);
    gtk_button_set_focus_on_click (self->close_button, FALSE);
    gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET(self->close_button), 
            FALSE, FALSE, 0);
    g_signal_connect (G_OBJECT (self->close_button), "clicked",
            G_CALLBACK (on_close_button_clicked), self);
    gtk_widget_show ( GTK_WIDGET(self->close_button) );

    // table for all the widgets
    self->table = GTK_TABLE( gtk_table_new( 1, 3, FALSE ) );
    gtk_container_set_border_width( GTK_CONTAINER(vbox), 2 );
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(self->table), 
            FALSE, FALSE, 0);
    //gtk_widget_show(GTK_WIDGET(self->table));
    self->trows = 0;

//    // arrow bin for the output format combo box
//    self->arrow_bin = gtk_arrow_bin_new ();
//    gtk_box_pack_start (GTK_BOX (vbox_outer), self->arrow_bin,
//            FALSE, FALSE, 0);
//    gtk_widget_show (self->arrow_bin);
    // output formats selector
    self->formats_combo = GTK_COMBO_BOX(gtk_combo_box_new_text());
    GtkWidget *fmtlabel = gtk_label_new("Format:");
    gtk_misc_set_alignment (GTK_MISC (fmtlabel), 1, 0.5);

    GtkWidget * fhbox = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (fhbox), fmtlabel, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (fhbox), GTK_WIDGET (self->formats_combo),
            TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), fhbox, FALSE, FALSE, 0);
    gtk_widget_show (fmtlabel);
    gtk_widget_show (GTK_WIDGET (self->formats_combo));
    gtk_widget_show (fhbox);

    self->ctl_info = g_hash_table_new_full( NULL, NULL, NULL, free );

    self->status_changed_handler_id = 0;
    self->formats_changed_handler_id = 0;

    self->unit = NULL;
}

static void
cam_unit_control_widget_class_init( CamUnitControlWidgetClass *klass )
{
    dbg(DBG_GUI, "unit control widget class initializer\n");
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    // add a class-specific destructor
    gobject_class->finalize = cam_unit_control_widget_finalize;

    unit_control_widget_signals[CLOSE_BUTTON_CLICKED_SIGNAL] = 
        g_signal_new("close-button-clicked",
            G_TYPE_FROM_CLASS(klass),
            G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
            0,
            NULL,
            NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE, 0);
}

// destructor (more or less)
static void
cam_unit_control_widget_finalize( GObject *obj )
{
    CamUnitControlWidget *self = CAM_UNIT_CONTROL_WIDGET( obj );
    dbg(DBG_GUI, "unit control widget finalize (%s)\n",
            self->unit ?  cam_unit_get_id(self->unit) : NULL );

    g_hash_table_destroy( self->ctl_info );
    self->ctl_info = NULL;

    cam_unit_control_widget_detach( self );

    G_OBJECT_CLASS (cam_unit_control_widget_parent_class)->finalize(obj);
}

CamUnitControlWidget *
cam_unit_control_widget_new( CamUnit *unit )
{
    CamUnitControlWidget * self = 
        CAM_UNIT_CONTROL_WIDGET( 
                g_object_new(CAM_TYPE_UNIT_CONTROL_WIDGET, NULL));
    cam_unit_control_widget_set_unit( self, unit );
    return self;
}

static int
num_chars (int n)
{
    int i = 0;
    if (n <= 0) { i++; n = -n; }
    while (n != 0) { n /= 10; i++; }
    return i;
}

static void
set_slider_label(CamUnitControlWidget *self, ControlWidgetInfo *ci)
{
    if (ci->labelval) {
        char str[20];
        if (!ci->use_int)
            sprintf (str, "%*.*f", ci->ctl->display_width,
                    ci->ctl->display_prec,
                    gtk_range_get_value (GTK_RANGE(ci->widget)));
        else
            sprintf (str, "%*d", ci->maxchars, 
                    (int) gtk_range_get_value (GTK_RANGE(ci->widget)));
        gtk_label_set_text (GTK_LABEL(ci->labelval), str);
    }
}

static void
on_slider_changed( GtkRange *range, CamUnitControlWidget *self )
{
    if( ! self->unit ) return;

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(range), 
                "ControlWidgetInfo" );
    CamUnitControl *ctl = ci->ctl;

    if (ctl->type == CAM_UNIT_CONTROL_TYPE_INT) {
        int newval = (int) gtk_range_get_value (range);
        int oldval = cam_unit_control_get_int( ctl );

        if( oldval != newval ) {
            cam_unit_control_try_set_int( ctl, newval );
        }

        // was the control successfully set?
        oldval = cam_unit_control_get_int( ctl );

        if( oldval != newval ) {
            gtk_range_set_value (GTK_RANGE (range), oldval);
            set_slider_label (self, ci);
        }
    }
    else if (ctl->type == CAM_UNIT_CONTROL_TYPE_FLOAT) {
        float newval = gtk_range_get_value (range);
        float oldval = cam_unit_control_get_float( ctl );

        if( oldval != newval ) {
            cam_unit_control_try_set_float( ctl, newval );
        }

        // was the control successfully set?
        oldval = cam_unit_control_get_float( ctl );

        if( oldval != newval ) {
            gtk_range_set_value (GTK_RANGE (range), oldval);
            set_slider_label (self, ci);
        }
    }
}

static void
control_set_sensitive (ControlWidgetInfo * ci)
{
    switch( ci->ctl->type ) {
        case CAM_UNIT_CONTROL_TYPE_INT:
        case CAM_UNIT_CONTROL_TYPE_BOOLEAN:
        case CAM_UNIT_CONTROL_TYPE_FLOAT:
        case CAM_UNIT_CONTROL_TYPE_ENUM:
            gtk_widget_set_sensitive( ci->widget, ci->ctl->enabled );
            if (ci->label)
                gtk_widget_set_sensitive( ci->label, ci->ctl->enabled );
            if (ci->labelval)
                gtk_widget_set_sensitive( ci->labelval, ci->ctl->enabled );
            break;
        case CAM_UNIT_CONTROL_TYPE_STRING:
            gtk_widget_set_sensitive( ci->widget, ci->ctl->enabled );

            if ( cam_unit_control_get_ui_hints( ci->ctl ) & 
                    CAM_UNIT_CONTROL_FILENAME ) {
                gtk_widget_set_sensitive( ci->file_chooser_bt,
                        ci->ctl->enabled );
            }
            break;
        default:
            err("UnitControlWidget:  unrecognized control type %d\n",
                    ci->ctl->type);
            break;
    }
}

static void
add_slider (CamUnitControlWidget * self,
        float min, float max, float step, float initial_value,
        int use_int, CamUnitControl * ctl)
{
    // label
    char *tmp = g_strjoin( "", ctl->name, ":", NULL);
    GtkWidget *label = gtk_label_new(tmp);
    free(tmp);
    gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);
    gtk_table_attach (self->table, label, 0, 1,
            self->trows, self->trows+1,
            GTK_FILL, 0, 0, 0);
    gtk_widget_show (label);
    GtkWidget *range = gtk_hscale_new_with_range(min, max, step);
    gtk_scale_set_draw_value(GTK_SCALE(range), FALSE);

    // slider widget

    /* This is a hack to use always round the HScale to integer
     * values.  Strangely, this functionality is normally only
     * available when draw_value is TRUE. */
    if (use_int)
        GTK_RANGE (range)->round_digits = 0;

    gtk_table_attach (self->table, range, 1, 2,
            self->trows, self->trows+1,
            GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0, 0);
    gtk_widget_show (range);
    gtk_range_set_value (GTK_RANGE (range), initial_value);

    // numerical label
    GtkWidget *labelval = gtk_label_new (NULL);
    gtk_table_attach (self->table, labelval, 2, 3,
            self->trows, self->trows+1,
            GTK_FILL, 0, 0, 0);
    PangoFontDescription * desc = pango_font_description_new ();
    pango_font_description_set_family_static (desc, "monospace");
    gtk_widget_modify_font (labelval, desc);
    gtk_misc_set_alignment (GTK_MISC(labelval), 1, 0.5);
    gtk_widget_show (labelval);

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) calloc(1, sizeof(ControlWidgetInfo));
    g_object_set_data( G_OBJECT(range), "ControlWidgetInfo", ci );
    ci->widget = range;
    ci->labelval = labelval;
    ci->label = label;
    ci->maxchars = MAX (num_chars (min), num_chars (max));
    ci->ctl = ctl;
    ci->use_int = use_int;
    g_hash_table_insert( self->ctl_info, ctl, ci );
    control_set_sensitive (ci);

    set_slider_label (self, ci);
    g_signal_connect (G_OBJECT (range), "value-changed",
            G_CALLBACK (on_slider_changed), self);

    self->trows++;
}

static void
on_spin_button_changed( GtkSpinButton *spinb, CamUnitControlWidget *self )
{
    if( ! self->unit ) return;

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(spinb), 
                "ControlWidgetInfo" );
    CamUnitControl *ctl = ci->ctl;

    if (ctl->type == CAM_UNIT_CONTROL_TYPE_INT) {
        int newval = (int) gtk_spin_button_get_value (spinb);
        int oldval = cam_unit_control_get_int( ctl );

        if( oldval != newval ) {
            cam_unit_control_try_set_int( ctl, newval );
        }

        // was the control successfully set?
        oldval = cam_unit_control_get_int( ctl );

        if( oldval != newval ) {
            gtk_spin_button_set_value (spinb, oldval);
        }
    }
    else if (ctl->type == CAM_UNIT_CONTROL_TYPE_FLOAT) {
        float newval = gtk_spin_button_get_value (spinb);
        float oldval = cam_unit_control_get_float( ctl );

        if( oldval != newval ) {
            cam_unit_control_try_set_float( ctl, newval );
        }

        // was the control successfully set?
        oldval = cam_unit_control_get_float( ctl );

        if( oldval != newval ) {
            gtk_spin_button_set_value (spinb, oldval);
        }
    }
}

static void
add_spinbutton (CamUnitControlWidget * self,
        float min, float max, float step, float initial_value,
        int use_int, CamUnitControl * ctl)
{
    // label
    char *tmp = g_strjoin( "", ctl->name, ":", NULL);
    GtkWidget *label = gtk_label_new(tmp);
    free(tmp);
    gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);
    gtk_table_attach (self->table, label, 0, 1,
            self->trows, self->trows+1,
            GTK_FILL, 0, 0, 0);
    gtk_widget_show (label);

    // spinbutton widget

    GtkWidget *spinb = gtk_spin_button_new_with_range (min, max, step);

    gtk_table_attach (self->table, spinb, 1, 3,
            self->trows, self->trows+1,
            GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0, 0);
    gtk_widget_show (spinb);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinb), initial_value);

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) calloc(1, sizeof(ControlWidgetInfo));
    g_object_set_data( G_OBJECT(spinb), "ControlWidgetInfo", ci );
    ci->widget = spinb;
    ci->labelval = NULL;
    ci->label = label;
    ci->maxchars = MAX (num_chars (min), num_chars (max));
    ci->ctl = ctl;
    ci->use_int = use_int;
    g_hash_table_insert( self->ctl_info, ctl, ci );
    control_set_sensitive (ci);

    set_slider_label (self, ci);
    g_signal_connect (G_OBJECT (spinb), "value-changed",
            G_CALLBACK (on_spin_button_changed), self);

    self->trows++;
}

static void
add_float_control (CamUnitControlWidget * self, CamUnitControl * ctl)
{
    add_slider (self, ctl->min_float, ctl->max_float,
            ctl->step_float, cam_unit_control_get_float (ctl),
            0, ctl);
}

static void
add_integer_control( CamUnitControlWidget *self, CamUnitControl *ctl )
{
    if( ctl->step_int == 0 ) {
        err("UnitControlWidget: refusing to add a widget for integer\n"
            "                   control [%s] with step 0\n", ctl->name);
        return;
    }
    int ui_hints = cam_unit_control_get_ui_hints( ctl );
    if (ui_hints & CAM_UNIT_CONTROL_SPINBUTTON) {
        add_spinbutton (self, ctl->min_int, ctl->max_int, ctl->step_int,
                cam_unit_control_get_int (ctl), 1, ctl);
    } else {
        add_slider (self, ctl->min_int, ctl->max_int, ctl->step_int,
                cam_unit_control_get_int (ctl), 1, ctl);
    }
}

static void
on_boolean_ctl_clicked( GtkWidget *cb, CamUnitControlWidget *self )
{
    if( ! self->unit ) return;

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(cb), 
                "ControlWidgetInfo" );
    CamUnitControl *ctl = ci->ctl;

    cam_unit_control_try_set_boolean( ctl, TRUE );
}

static void
on_boolean_ctl_changed( GtkWidget *cb, CamUnitControlWidget *self )
{
    if( ! self->unit ) return;

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(cb), 
                "ControlWidgetInfo" );
    CamUnitControl *ctl = ci->ctl;

    int newval = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(cb) );

    int oldval = cam_unit_control_get_boolean( ctl );
    if( oldval != newval ) {
        cam_unit_control_try_set_boolean( ctl, newval );
    }

    // was the control successfully set?
    oldval = cam_unit_control_get_boolean( ctl );
    if( oldval != newval ) {
        // nope.  re-set the toggle button
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(cb), 
                oldval ? TRUE : FALSE );
    }
}

static void
add_boolean_ctl_helper( CamUnitControlWidget *self, CamUnitControl *ctl,
        GtkWidget *cb ) 
{
    gtk_widget_show (cb);

    gtk_table_attach (GTK_TABLE (self->table), cb, 1, 3,
            self->trows, self->trows+1,
            GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0, 0);

    if (GTK_IS_TOGGLE_BUTTON (cb)) {
        int val = cam_unit_control_get_boolean(ctl);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cb),
                val ? TRUE : FALSE);
    }

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) calloc(1, sizeof(ControlWidgetInfo));
    g_object_set_data( G_OBJECT(cb), "ControlWidgetInfo", ci );
    ci->widget = cb;
    ci->label = NULL;
    ci->labelval = NULL;
    ci->maxchars = 0;
    ci->ctl = ctl;
    g_hash_table_insert( self->ctl_info, ctl, ci );

    control_set_sensitive (ci);

    if (GTK_IS_TOGGLE_BUTTON (cb)) {
        g_signal_connect (G_OBJECT (cb), "toggled",
                G_CALLBACK (on_boolean_ctl_changed), self);
    }
    else {
        g_signal_connect (G_OBJECT (cb), "clicked",
                G_CALLBACK (on_boolean_ctl_clicked), self);
    }

    self->trows++;
}

static void
add_boolean_control( CamUnitControlWidget *self, CamUnitControl *ctl )
{
    int ui_hints = cam_unit_control_get_ui_hints( ctl );
    GtkWidget *widget = NULL;
    if (ui_hints & CAM_UNIT_CONTROL_ONE_SHOT)
        widget = gtk_button_new_with_label (ctl->name);
    else if (ui_hints & CAM_UNIT_CONTROL_TOGGLE_BUTTON)
        widget = gtk_toggle_button_new_with_label (ctl->name);
    else
        widget = gtk_check_button_new_with_label (ctl->name);

    add_boolean_ctl_helper( self, ctl, widget );
}

static void
on_menu_changed (GtkComboBox * combo, CamUnitControlWidget *self)
{
    if( ! self->unit ) return;

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(combo), 
                "ControlWidgetInfo" );
    CamUnitControl *ctl = ci->ctl;

    int oldval = cam_unit_control_get_enum( ctl );
    int newval = gtk_combo_box_get_active (combo);
    
    if( oldval != newval ) {
        cam_unit_control_try_set_enum( ctl, newval );
    }

    // was the control successfully set?
    oldval = cam_unit_control_get_enum( ctl );

    if( oldval != newval ) {
        gtk_combo_box_set_active( combo, oldval );
    }
}

static void
add_menu_control( CamUnitControlWidget *self, CamUnitControl *ctl )
{
    int j;

    // label
    char *tmp = g_strjoin( "", ctl->name, ":", NULL);
    GtkWidget *label = gtk_label_new(tmp);
    free(tmp);
    gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);
    gtk_table_attach (self->table, label, 0, 1,
            self->trows, self->trows+1,
            GTK_FILL, 0, 0, 0);
    gtk_widget_show (label);

    GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING,
            G_TYPE_BOOLEAN);
    for (j = 0; j <= ctl->max_int; j++) {
        GtkTreeIter iter;
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                0, ctl->menu_entries[j],
                1, ctl->menu_entries_enabled ? ctl->menu_entries_enabled[j] : 1,
                -1);
    }

    GtkWidget *mb = gtk_combo_box_new_with_model (GTK_TREE_MODEL (store));
    GtkCellRenderer * renderer = gtk_cell_renderer_text_new ();
    g_object_set (G_OBJECT (renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (mb), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (mb), renderer,
            "text", 0);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (mb), renderer,
            "sensitive", 1);

    g_object_unref (store);

#if 0
    GtkWidget *mb = gtk_combo_box_new_text ();
    for (j = 0; j <= ctl->max_int; j++) {
        gtk_combo_box_append_text (GTK_COMBO_BOX (mb), ctl->menu_entries[j]);
    }
#endif

    gtk_table_attach (self->table, mb, 1, 3,
            self->trows, self->trows+1,
            GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0, 0);
    gtk_widget_show (mb);

    int val = cam_unit_control_get_enum( ctl );
    gtk_combo_box_set_active (GTK_COMBO_BOX (mb), val);

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) calloc(1, sizeof(ControlWidgetInfo));
    g_object_set_data( G_OBJECT(mb), "ControlWidgetInfo", ci );
    ci->widget = mb;
    ci->label = label;
    ci->labelval = NULL;
    ci->maxchars = 0;
    ci->ctl = ctl;
    g_hash_table_insert( self->ctl_info, ctl, ci );

    control_set_sensitive (ci);

    g_signal_connect (G_OBJECT (mb), "changed", G_CALLBACK (on_menu_changed), 
            self);

    self->trows++;
}

static void
on_file_entry_choser_bt_clicked(GtkButton *button, CamUnitControlWidget *self)
{
    if( ! self->unit ) return;

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(button), 
                "ControlWidgetInfo" );
    CamUnitControl *ctl = ci->ctl;

    GtkWidget *dialog;
    dialog = gtk_file_chooser_dialog_new ("New Log File",
            GTK_WINDOW( gtk_widget_get_toplevel(GTK_WIDGET(self)) ),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
            NULL);
    char *newval = NULL;
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        newval = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        dbg(DBG_GUI, "user chose file: [%s]\n", newval);
        gtk_widget_destroy (dialog);
    } else {
        gtk_widget_destroy (dialog);
        return;
    }

    const char *oldval = cam_unit_control_get_string( ctl );
    
    if( !oldval || strcmp( newval, oldval ) ) {
        cam_unit_control_try_set_string( ctl, newval );
    }

    oldval = cam_unit_control_get_string( ctl );
    gtk_entry_set_text( GTK_ENTRY(ci->widget), oldval );
    free( newval );
}

static void
add_string_control_filename( CamUnitControlWidget *self, CamUnitControl *ctl )
{
    // label
    char *tmp = g_strjoin( "", ctl->name, ":", NULL);
    GtkWidget *label = gtk_label_new(tmp);
    free(tmp);
    gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);
    gtk_table_attach (self->table, label, 0, 1,
            self->trows, self->trows+1,
            GTK_FILL, 0, 0, 0);
    gtk_widget_show (label);

    // hbox to contain the text entry and file chooser button
    GtkWidget *hbox = gtk_hbox_new( FALSE, 0 );
    gtk_table_attach( self->table, hbox, 1, 3, 
            self->trows, self->trows+1,
            GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0, 0 );
    gtk_widget_show(hbox);

    // text entry
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text( GTK_ENTRY(entry), cam_unit_control_get_string(ctl) );
    gtk_box_pack_start( GTK_BOX(hbox), entry, FALSE, FALSE, 0 );
    gtk_entry_set_editable( GTK_ENTRY(entry), FALSE );
    gtk_widget_show( entry );

    // file chooser button
    GtkWidget *chooser_bt = gtk_button_new_from_stock( GTK_STOCK_OPEN );
    gtk_box_pack_start( GTK_BOX(hbox), chooser_bt, FALSE, TRUE, 0 );
    gtk_widget_show( chooser_bt );

    g_signal_connect( G_OBJECT(chooser_bt), "clicked", 
            G_CALLBACK(on_file_entry_choser_bt_clicked), self );

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) calloc(1, sizeof(ControlWidgetInfo));
    g_object_set_data( G_OBJECT(entry), "ControlWidgetInfo", ci );
    g_object_set_data( G_OBJECT(chooser_bt), "ControlWidgetInfo", ci );
    ci->widget = entry;
    ci->file_chooser_bt = chooser_bt;
    ci->label = label;
    ci->labelval = NULL;
    ci->maxchars = -1;
    ci->ctl = ctl;
    g_hash_table_insert( self->ctl_info, ctl, ci );
    self->trows++;
}

static void
on_string_control_clicked (GtkButton * button, CamUnitControlWidget * self)
{
    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(button), 
                "ControlWidgetInfo" );
    CamUnitControl *ctl = ci->ctl;
    cam_unit_control_try_set_string (ctl,
            gtk_entry_get_text (GTK_ENTRY (ci->widget)));

    gtk_entry_set_text (GTK_ENTRY (ci->widget),
        cam_unit_control_get_string (ctl));

    /* Move cursor to end */
    gtk_editable_set_position (GTK_EDITABLE (ci->widget), -1);
}

static void
on_string_control_activated (GtkEntry * entry, CamUnitControlWidget * self)
{
    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(entry), 
                "ControlWidgetInfo" );
    gtk_button_clicked (GTK_BUTTON (ci->button));
}

static gboolean
on_string_control_key (GtkEntry * entry, GdkEventKey * event,
        CamUnitControlWidget * self)
{
    if (event->keyval != GDK_Escape)
        return FALSE;

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) g_object_get_data(G_OBJECT(entry), 
                "ControlWidgetInfo" );
    CamUnitControl *ctl = ci->ctl;
    /* Restore entry to its previous value */
    gtk_entry_set_text (GTK_ENTRY (entry), cam_unit_control_get_string (ctl));

    /* Move cursor to end */
    gtk_editable_set_position (GTK_EDITABLE (entry), -1);
    return FALSE;
}

static void
add_string_control_entry( CamUnitControlWidget *self, CamUnitControl *ctl )
{
    // label
    char *tmp = g_strjoin( "", ctl->name, ":", NULL);
    GtkWidget *label = gtk_label_new(tmp);
    free(tmp);
    gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);
    gtk_table_attach (self->table, label, 0, 1,
            self->trows, self->trows+1,
            GTK_FILL, 0, 0, 0);
    gtk_widget_show (label);

    GtkWidget *hbox = gtk_hbox_new (FALSE, 1);
    // text entry
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text( GTK_ENTRY(entry), cam_unit_control_get_string(ctl) );
    gtk_box_pack_start (GTK_BOX (hbox), entry, TRUE, TRUE, 0);
    //gtk_entry_set_editable( GTK_ENTRY(entry), FALSE );
    
    GtkWidget *button = gtk_button_new_with_label ("Set");
    gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);

    gtk_table_attach( self->table, hbox, 1, 3, 
            self->trows, self->trows+1,
            GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0, 0 );

    gtk_widget_show_all( hbox );

    g_signal_connect (G_OBJECT (entry), "activate",
            G_CALLBACK (on_string_control_activated), self);
    g_signal_connect (G_OBJECT (entry), "key-press-event",
            G_CALLBACK (on_string_control_key), self);
    g_signal_connect (G_OBJECT (button), "clicked",
            G_CALLBACK (on_string_control_clicked), self);

    ControlWidgetInfo *ci = 
        (ControlWidgetInfo*) calloc(1, sizeof(ControlWidgetInfo));
    g_object_set_data( G_OBJECT(entry), "ControlWidgetInfo", ci );
    g_object_set_data( G_OBJECT(button), "ControlWidgetInfo", ci );
    ci->widget = entry;
    ci->label = label;
    ci->labelval = NULL;
    ci->button = button;
    ci->maxchars = -1;
    ci->ctl = ctl;
    g_hash_table_insert( self->ctl_info, ctl, ci );
    self->trows++;

    control_set_sensitive (ci);
}

static void
add_string_control( CamUnitControlWidget *self, CamUnitControl *ctl )
{
    int ui_hints = cam_unit_control_get_ui_hints( ctl );
    if( ui_hints & CAM_UNIT_CONTROL_FILENAME ) {
        add_string_control_filename( self, ctl );
    } else {
        add_string_control_entry( self, ctl );
        // TODO
    }
}

static void
on_control_value_changed(CamUnit *unit, CamUnitControl *ctl, 
        CamUnitControlWidget *self)
{
    if( ! self->unit ) return;

    ControlWidgetInfo *ci = g_hash_table_lookup( self->ctl_info, ctl );
    if( ci ) {
        GValue val = { 0, };
        cam_unit_control_get_val( ctl, &val );

        switch( ctl->type ) {
            case CAM_UNIT_CONTROL_TYPE_INT:
                if (GTK_IS_SPIN_BUTTON (ci->widget)) {
//                    printf("set value %d\n", g_value_get_int(&val));
                    gtk_spin_button_set_value (GTK_SPIN_BUTTON(ci->widget),
                        g_value_get_int(&val));
                } else if (GTK_IS_RANGE (ci->widget)) {
                    gtk_range_set_value (GTK_RANGE(ci->widget), 
                            g_value_get_int(&val));
                    set_slider_label(self, ci);
                } else {
                    err ("wtf?? %s:%d", __FILE__, __LINE__);
                }
                break;
            case CAM_UNIT_CONTROL_TYPE_FLOAT:
                gtk_range_set_value (GTK_RANGE(ci->widget), 
                        g_value_get_float(&val));
                set_slider_label(self, ci);
                break;
            case CAM_UNIT_CONTROL_TYPE_BOOLEAN:
                if (GTK_IS_TOGGLE_BUTTON (ci->widget))
                    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(ci->widget),
                            g_value_get_boolean(&val) ? TRUE : FALSE );
                break;
            case CAM_UNIT_CONTROL_TYPE_ENUM:
                gtk_combo_box_set_active( GTK_COMBO_BOX(ci->widget), 
                        g_value_get_int(&val) );
                break;
            case CAM_UNIT_CONTROL_TYPE_STRING:
                gtk_entry_set_text( GTK_ENTRY(ci->widget), 
                        g_value_get_string(&val) );
                break;
            default:
                err("UnitControlWidget:  unrecognized control type %d\n",
                        ctl->type);
                break;
        }
        g_value_unset( &val );
    }
}

static void
on_control_parameters_changed(CamUnit *unit, CamUnitControl *ctl,
        CamUnitControlWidget *self)
{
    if( ! self->unit ) return;
//    int enabled = cam_unit_control_get_enabled( ctl );
//    printf("control enabled changed [%s] to %d\n", ctl->name, enabled);

    ControlWidgetInfo *ci = g_hash_table_lookup( self->ctl_info, ctl );
    if( ci )
        control_set_sensitive (ci);
}

static void
set_frame_label( CamUnitControlWidget *self )
{
    if (self->unit) {
        const char *uname = cam_unit_get_name(self->unit);
        const char *sstr = 
            cam_unit_status_to_str(cam_unit_get_status(self->unit));
        char *tmp = g_strjoin( "", uname, " [", sstr, "]", NULL );
        gtk_label_set (GTK_LABEL (self->exp_label), tmp);
        free(tmp);
    } else {
        gtk_label_set (GTK_LABEL (self->exp_label), "INVALID UNIT");
    }
}

static void
update_formats_combo( CamUnitControlWidget *self )
{
    for( ; self->formats_combo_nentries; self->formats_combo_nentries-- ) {
        gtk_combo_box_remove_text( self->formats_combo, 0 );
    }

    if (! self->unit) return;

    const CamUnitFormat *out_fmt = cam_unit_get_output_format( self->unit );
    GList *output_formats = cam_unit_get_output_formats( self->unit );
    GList *fiter;
    int selected = -1;
    self->formats_combo_nentries = 0;
    for( fiter=output_formats; fiter; fiter=fiter->next ) {
        CamUnitFormat *fmt = (CamUnitFormat*) fiter->data;
        gtk_combo_box_append_text( self->formats_combo, 
                fmt->name );

        if( fmt == out_fmt ) {
            selected = self->formats_combo_nentries;
        }
        self->formats_combo_nentries++;
    }
    gtk_combo_box_set_active( GTK_COMBO_BOX(self->formats_combo), selected );
}

int
cam_unit_control_widget_set_unit( 
        CamUnitControlWidget *self, CamUnit *unit)
{
    if( self->unit ) return -1;
    dbg(DBG_GUI, "UDCW: setting to [%s]\n", 
            cam_unit_get_id(unit));

    if (unit) {
        dbgl(DBG_REF, "ref_sink unit [%s]\n", cam_unit_get_id(unit));
        g_object_ref_sink (unit);
    }

    self->unit = unit;
    set_frame_label( self );

    // prepare the output formats selection combo box
    if (self->unit) {
        self->formats_changed_handler_id = 
            g_signal_connect( G_OBJECT(unit), "output-formats-changed",
                    G_CALLBACK(on_output_formats_changed), self );
        self->status_changed_handler_id = 
            g_signal_connect( G_OBJECT(unit), "status-changed",
                    G_CALLBACK(on_status_changed), self );
        g_signal_connect( G_OBJECT(self->formats_combo), "changed",
                G_CALLBACK(on_formats_combo_changed), self );
    }

    update_formats_combo(self);

//    gtk_container_add (GTK_CONTAINER (self->arrow_bin), hbox);

#if 0
    gtk_table_attach (self->table, fmtlabel, 0, 1, 0, 1,
            GTK_FILL, 0, 0, 0);
    gtk_widget_show (fmtlabel);
    gtk_table_attach_defaults( self->table, GTK_WIDGET(self->formats_combo), 
            1, 3, 0, 1 );
#endif

    // prepare the unit controls widgets
    if (self->unit) {
        GList *controls = cam_unit_get_controls( unit );
        GList *citer;
        for( citer=controls; citer; citer=citer->next ) {
            CamUnitControl *ctl = (CamUnitControl*) citer->data;

            dbg(DBG_GUI, "adding widget for [%s] of [%s]\n", 
                    ctl->name, cam_unit_get_id(self->unit) );

            switch( ctl->type ) {
                case CAM_UNIT_CONTROL_TYPE_INT:
                    add_integer_control( self, ctl );
                    break;
                case CAM_UNIT_CONTROL_TYPE_FLOAT:
                    add_float_control (self, ctl);
                    break;
                case CAM_UNIT_CONTROL_TYPE_BOOLEAN:
                    add_boolean_control( self, ctl );
                    break;
                case CAM_UNIT_CONTROL_TYPE_ENUM:
                    add_menu_control( self, ctl );
                    break;
                case CAM_UNIT_CONTROL_TYPE_STRING:
                    add_string_control( self, ctl );
                    break;
                default:
                    err("UnitControlWidget:  unrecognized control type %d\n",
                            ctl->type);
                    break;
            }
        }
        g_list_free( controls );

        g_signal_connect( G_OBJECT(unit), "control-value-changed",
                G_CALLBACK(on_control_value_changed), self );
        g_signal_connect( G_OBJECT(unit), "control-parameters-changed",
                G_CALLBACK(on_control_parameters_changed), self );
    }

    //gtk_widget_show_all( GTK_WIDGET(self->table) );

    gtk_expander_set_expanded( self->expander, FALSE );
    return 0;
}

void 
cam_unit_control_widget_detach( CamUnitControlWidget *self )
{
    if( ! self->unit ) return;
    dbg(DBG_GUI, "detaching control widget from unit signals\n");

    g_signal_handlers_disconnect_by_func (G_OBJECT (self->unit),
            G_CALLBACK(on_control_value_changed), self);
    g_signal_handlers_disconnect_by_func (G_OBJECT (self->unit),
            G_CALLBACK(on_control_parameters_changed), self );
    g_signal_handler_disconnect( self->unit, 
            self->status_changed_handler_id );
    g_signal_handler_disconnect( self->unit, 
            self->formats_changed_handler_id );
    dbgl(DBG_REF, "unref unit\n");
    g_object_unref (self->unit);
    self->unit = NULL;
}

static void
on_output_formats_changed( CamUnit *unit, CamUnitControlWidget *self )
{
    dbg(DBG_GUI, "detected changed output formats for [%s]\n", 
            cam_unit_get_id( unit ));

    // TODO
}

static void
on_formats_combo_changed( GtkComboBox *combo, CamUnitControlWidget *self )
{
    int selected = gtk_combo_box_get_active (combo);
    if (selected < 0)
        return;

    GList *output_formats = cam_unit_get_output_formats( self->unit );
    if (!output_formats)
        return;

    GList *format_entry = g_list_nth (output_formats, selected);
    g_assert (format_entry);
    if (format_entry->data != cam_unit_get_output_format (self->unit)) {
        CamUnitStatus orig_status = cam_unit_get_status (self->unit);
        cam_unit_stream_set_preferred_format (self->unit, format_entry->data);
        if (orig_status != CAM_UNIT_STATUS_IDLE) {
            cam_unit_stream_shutdown (self->unit);
            cam_unit_stream_init_any_format (self->unit);
        }
        if (orig_status == CAM_UNIT_STATUS_STREAMING)
            cam_unit_stream_on (self->unit);
    }
}

static void
on_close_button_clicked( GtkButton *bt, CamUnitControlWidget *self )
{
    g_signal_emit( G_OBJECT(self), 
            unit_control_widget_signals[CLOSE_BUTTON_CLICKED_SIGNAL], 0 );
}

static void
on_expander_notify( GtkWidget *widget, GParamSpec *param, 
        CamUnitControlWidget *self )
{
    if( gtk_expander_get_expanded( self->expander ) ) {
        gtk_widget_show( GTK_WIDGET(self->table) );
    } else {
        gtk_widget_hide( GTK_WIDGET(self->table) );
    }
}

static void
on_status_changed( CamUnit *unit, int old_status, CamUnitControlWidget *self )
{
    set_frame_label( self );
    update_formats_combo( self );
}

void
cam_unit_control_widget_set_expanded (CamUnitControlWidget * self,
        gboolean expanded)
{
    gtk_expander_set_expanded (GTK_EXPANDER (self->expander), expanded);
}