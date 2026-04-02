#!/bin/bash

# Source and destination directories
SRC_DIR="/local/repository/PDCP_reordering"
DST_DIR="/local/repository/openairinterface5g"

echo "Copying PDCP reordering files..."

cp "$SRC_DIR/gnb_paramdef.h" "$DST_DIR/openair2/GNB_APP/gnb_paramdef.h" 
cp "$SRC_DIR/gnb_config.c" "$DST_DIR/openair2/GNB_APP/gnb_config.c" 

cp "$SRC_DIR/nr_pdcp_configuration.h" "$DST_DIR/openair2/LAYER2/nr_pdcp/nr_pdcp_configuration.h" 
cp "$SRC_DIR/nr_pdcp_entity.c" "$DST_DIR/openair2/LAYER2/nr_pdcp/nr_pdcp_entity.c" 
cp "$SRC_DIR/nr_pdcp_entity.h" "$DST_DIR/openair2/LAYER2/nr_pdcp/nr_pdcp_entity.h" 
cp "$SRC_DIR/cucp_cuup_handler.c" "$DST_DIR/openair2/LAYER2/nr_pdcp/cucp_cuup_handler.c" 
cp "$SRC_DIR/nr_pdcp_oai_api.c" "$DST_DIR/openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.c" 
cp "$SRC_DIR/nr_pdcp_sdu.c" "$DST_DIR/openair2/LAYER2/nr_pdcp/nr_pdcp_sdu.c" 
cp "$SRC_DIR/nr_pdcp_sdu.h" "$DST_DIR/openair2/LAYER2/nr_pdcp/nr_pdcp_sdu.h" 

cp "$SRC_DIR/e1ap_messages_types.h" "$DST_DIR/openair2/COMMON/e1ap_messages_types.h" 
cp "$SRC_DIR/e1ap_bearer_context_management.c" "$DST_DIR/openair2/E1AP/lib/e1ap_bearer_context_management.c" 
cp "$SRC_DIR/rrc_gNB_radio_bearers.c" "$DST_DIR/openair2/RRC/NR/rrc_gNB_radio_bearers.c" 
cp "$SRC_DIR/asn1_msg.c" "$DST_DIR/openair2/RRC/NR/MESSAGES/asn1_msg.c" 
cp "$SRC_DIR/nr_rrc_defs.h" "$DST_DIR/openair2/RRC/NR/nr_rrc_defs.h" 

echo "Copy complete."
