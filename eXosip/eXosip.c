/*
  eXosip - This is the eXtended osip library.
  Copyright (C) 2002, 2003  Aymeric MOIZARD  - jack@atosc.org
  
  eXosip is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  eXosip is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#ifdef ENABLE_MPATROL
#include <mpatrol.h>
#endif

#include <eXosip.h>
#include <eXosip2.h>

#include <osip2/thread.h>

#if defined WIN32
#include <winsock.h>
#else
// end andrea
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <unistd.h>

eXosip_t eXosip;
char    *localip;
char    *localport;
extern char *register_callid_number;


int
eXosip_lock()
{
  return osip_mutex_lock((struct osip_mutex*)eXosip.j_mutexlock);
}

int
eXosip_unlock()
{
  return osip_mutex_unlock((struct osip_mutex*)eXosip.j_mutexlock);
}

void
eXosip_kill_transaction (osip_list_t * transactions)
{
  osip_transaction_t *transaction;

  if (!osip_list_eol (transactions, 0))
    {
      /* some transaction are still used by osip,
         transaction should be released by modules! */
      OSIP_TRACE (osip_trace
		  (__FILE__, __LINE__, OSIP_ERROR, NULL,
		   "module sfp: _osip_kill_transaction transaction should be released by modules!\n"));
    }

  while (!osip_list_eol (transactions, 0))
    {
      transaction = osip_list_get (transactions, 0);

      __eXosip_delete_jinfo(transaction);
      osip_transaction_free (transaction);
      osip_free (transaction);
    }
}

void eXosip_quit()
{
  eXosip_call_t *jc;
  eXosip_reg_t  *jreg;
  int i;

  eXosip.j_stop_ua = 1; /* ask to quit the application */
  i = osip_thread_join((struct osip_thread*)eXosip.j_thread);
  if (i!=0)
    fprintf(stderr, "eXosip: can't terminate thread!");
  osip_free((struct osip_thread*)eXosip.j_thread);

  osip_free(localip);
  osip_free(localport);
  osip_free(register_callid_number);

  eXosip.j_input = 0;
  eXosip.j_output = 0;

  for (jc = eXosip.j_calls; jc!=NULL;jc = eXosip.j_calls)
    {
      REMOVE_ELEMENT(eXosip.j_calls, jc);
      eXosip_call_free(jc);
    }
  
  osip_mutex_destroy((struct osip_mutex*)eXosip.j_mutexlock);

  sdp_negotiation_free();  

  if (eXosip.j_input)
    fclose(eXosip.j_input);
  if (eXosip.j_output)
    osip_free(eXosip.j_output);
  if (eXosip.j_socket)
    close(eXosip.j_socket);

  for (jreg = eXosip.j_reg; jreg!=NULL; jreg = eXosip.j_reg)
    {
      REMOVE_ELEMENT(eXosip.j_reg, jreg);
      eXosip_reg_free(jreg);
    }

  /* should be moved to method with an argument */
  jfriend_unload();
  jidentity_unload();

  /*    
  for (jid = eXosip.j_identitys; jid!=NULL; jid = eXosip.j_identitys)
    {
      REMOVE_ELEMENT(eXosip.j_identitys, jid);
      eXosip_friend_free(jid);
    }

  for (jfr = eXosip.j_friends; jfr!=NULL; jfr = eXosip.j_friends)
    {
      REMOVE_ELEMENT(eXosip.j_friends, jfr);
      eXosip_reg_free(jfr);
    }
  */

  while (!osip_list_eol(eXosip.j_transactions, 0))
    {
      osip_transaction_t *tr = (osip_transaction_t*) osip_list_get(eXosip.j_transactions, 0);
      if (tr->state==IST_TERMINATED || tr->state==ICT_TERMINATED
	  || tr->state== NICT_TERMINATED || tr->state==NIST_TERMINATED)
	{
	  OSIP_TRACE(osip_trace(__FILE__,__LINE__,OSIP_INFO1,NULL,
		      "Release a terminated transaction\n"));
	  osip_list_remove(eXosip.j_transactions, 0);
	  __eXosip_delete_jinfo(tr);
	  osip_transaction_free2(tr);
	}
      else
	{
	  osip_list_remove(eXosip.j_transactions, 0);
	  __eXosip_delete_jinfo(tr);
	  osip_transaction_free(tr);
	}
    }

  osip_free(eXosip.j_transactions);

  eXosip_kill_transaction (eXosip.j_osip->osip_ict_transactions);
  eXosip_kill_transaction (eXosip.j_osip->osip_nict_transactions);
  eXosip_kill_transaction (eXosip.j_osip->osip_ist_transactions);
  eXosip_kill_transaction (eXosip.j_osip->osip_nist_transactions);
  osip_release (eXosip.j_osip);

  return ;
}

int eXosip_execute ( void )
{
  int i;
  i = eXosip_read_message(1, 0, 500000);
  if (i==-2)
    {
      return -2;
    }

  eXosip_lock();
  osip_timers_ict_execute(eXosip.j_osip);
  osip_timers_nict_execute(eXosip.j_osip);
  osip_timers_ist_execute(eXosip.j_osip);
  osip_timers_nist_execute(eXosip.j_osip);
  
  osip_ict_execute(eXosip.j_osip);
  osip_nict_execute(eXosip.j_osip);
  osip_ist_execute(eXosip.j_osip);
  osip_nist_execute(eXosip.j_osip);
  
  // free all Calls that are in the TERMINATED STATE? */
  eXosip_release_terminated_calls();

  eXosip_unlock();

  return 0;
}

void *eXosip_thread        ( void *arg )
{
  int i;
  while (eXosip.j_stop_ua==0)
    {
      i = eXosip_execute();
      if (i==-2)
	osip_thread_exit();
    }
  osip_thread_exit();
  return NULL;
}

int eXosip_init(FILE *input, FILE *output, int port)
{
  osip_t *osip;
  int i;
  if (port<0)
    {
      fprintf(stderr, "eXosip: port must be higher than 0!\n");
      return -1;
    }
  localip = (char *) osip_malloc(30);
  memset(localip, '\0', 30);
  eXosip_guess_ip_for_via(localip);
  if (localip[0]=='\0')
    {
#ifdef ENABLE_DEBUG
      fprintf(stderr, "eXosip: No ethernet interface found!\n");
      fprintf(stderr, "eXosip: using 127.0.0.1 (debug mode)!\n");
      strcpy(localip, "127.0.0.1");
#else
      fprintf(stderr, "eXosip: No ethernet interface found!\n");
      return -1;
#endif
    }

  eXosip.j_input = input;
  eXosip.j_output = output;
  eXosip.j_calls = NULL;
  eXosip.j_stop_ua = 0;
  eXosip.j_thread = NULL;
  eXosip.j_transactions = (osip_list_t*) osip_malloc(sizeof(osip_list_t));
  osip_list_init(eXosip.j_transactions);
  eXosip.j_reg = NULL;

  eXosip.j_mutexlock = (struct osip_mutex*)osip_mutex_init();

  if (-1==osip_init(&osip))
    {
      fprintf(stderr, "eXosip: Cannot initialize osip!\n");
      return -1;
    }

  eXosip_sdp_negotiation_init();

  osip_set_application_context(osip, &eXosip);
  
  eXosip_set_callbacks(osip);
  
  eXosip.j_osip = osip;

  // open the UDP listener
          
  eXosip.j_socket = (int)socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (eXosip.j_socket==-1)
    return -1;
  
  {
    struct sockaddr_in  raddr;
    raddr.sin_addr.s_addr = htons(INADDR_ANY);
    raddr.sin_port = htons((short)port);
    raddr.sin_family = AF_INET;
    
    i = bind(eXosip.j_socket, (struct sockaddr *)&raddr, sizeof(raddr));
    if (i < 0)
    {
      fprintf(stderr, "eXosip: Cannot bind on port: %i!\n", i);
      return -1;
    }
  }

  localport = (char*)osip_malloc(10);
  sprintf(localport, "%i", port);

  eXosip.j_thread = (void*) osip_thread_create(20000,eXosip_thread, NULL);
  if (eXosip.j_thread==NULL)
    {
      fprintf(stderr, "eXosip: Cannot start thread!\n");
      return -1;
    }

  return 0;
}

void
eXosip_update()
{
  eXosip_call_t      *jc;
  eXosip_subscribe_t *js;
  eXosip_notify_t *jn;
  eXosip_dialog_t    *jd;
  int counter=1;
  int counter2=1;
  for (jc=eXosip.j_calls; jc!=NULL; jc=jc->next)
    {
      jc->c_id = counter2;
      counter2++;
      for (jd=jc->c_dialogs; jd!=NULL; jd=jd->next)
	{
	  if (jd->d_dialog!=NULL) /* finished call */
	    {
	      jd->d_id = counter;
	      counter++;
	    }
	  else jd->d_id = -1;
	}
    }

  for (js=eXosip.j_subscribes; js!=NULL; js=js->next)
    {
      js->s_id = counter2;
      counter2++;
      for (jd=js->s_dialogs; jd!=NULL; jd=jd->next)
	{
	  if (jd->d_dialog!=NULL) /* finished call */
	    {
	      jd->d_id = counter;
	      counter++;
	    }
	  else jd->d_id = -1;
	}
    }

  for (jn=eXosip.j_notifies; jn!=NULL; jn=jn->next)
    {
      jn->n_id = counter2;
      counter2++;
      for (jd=jn->n_dialogs; jd!=NULL; jd=jd->next)
	{
	  if (jd->d_dialog!=NULL) /* finished call */
	    {
	      jd->d_id = counter;
	      counter++;
	    }
	  else jd->d_id = -1;
	}
    }
}

void eXosip_message    (char *to, char *from, char *route, char *buff)
{
  /* eXosip_call_t *jc;
     osip_header_t *subject; */
  osip_message_t *message;
  osip_transaction_t *transaction;
  osip_event_t *sipevent;
  int i;

  i = generating_message(&message, to, from, route, buff);
  if (i!=0) 
    {
      fprintf(stderr, "eXosip: cannot send message (cannot build MESSAGE)! ");
      return;
    }

  i = osip_transaction_init(&transaction,
		       NICT,
		       eXosip.j_osip,
		       message);
  if (i!=0)
    {
      /* TODO: release the j_call.. */

      msg_free(message);
      return ;
    }
  
  osip_list_add(eXosip.j_transactions, transaction, 0);
  
  sipevent = osip_new_outgoing_sipmessage(message);
  sipevent->transactionid =  transaction->transactionid;
  
  osip_transaction_add_event(transaction, sipevent);

  osip_transaction_set_your_instance(transaction, __eXosip_new_jinfo(NULL, NULL, NULL, NULL));
}

void eXosip_start_call    (osip_message_t *invite)
{
  //  static int static_jcid = 0; /* This value is used as a unique id for call */

  eXosip_call_t *jc;
  osip_header_t *subject;
  osip_transaction_t *transaction;
  osip_event_t *sipevent;
  int i;
  sdp_message_t *sdp;
  char *body;
  char *size;
  
  sdp_build_offer(NULL, &sdp, "10500", NULL);

  i = sdp_message_to_str(sdp, &body);
  if (body!=NULL)
    {
      size= (char *)osip_malloc(7*sizeof(char));
      sprintf(size,"%i",strlen(body));
      osip_parser_set_content_length(invite, size);
      osip_free(size);
      
      osip_parser_set_body(invite, body);
      osip_free(body);
      osip_parser_set_content_type(invite, "application/sdp");
    }
  else
    osip_parser_set_content_length(invite, "0");
  
  eXosip_call_init(&jc);
  i = osip_parser_get_subject(invite, 0, &subject);
  snprintf(jc->c_subject, 99, "%s", subject->hvalue);
  
  sdp_negotiation_ctx_set_mycontext(jc->c_ctx, jc);
  sdp_negotiation_ctx_set_local_sdp(jc->c_ctx, sdp);  

  i = osip_transaction_init(&transaction,
		       ICT,
		       eXosip.j_osip,
		       invite);
  if (i!=0)
    {
      /* TODO: release the j_call.. */
      msg_free(invite);
      return ;
    }
  
  jc->c_out_tr = transaction;
  
  sipevent = osip_new_outgoing_sipmessage(invite);
  sipevent->transactionid =  transaction->transactionid;
  
  osip_transaction_add_event(transaction, sipevent);

  osip_transaction_set_your_instance(transaction, __eXosip_new_jinfo(jc, NULL, NULL, NULL));
  ADD_ELEMENT(eXosip.j_calls, jc);
}

void eXosip_answer_call   (int jid, int status)
{
  eXosip_dialog_t *jd = NULL;
  eXosip_call_t *jc = NULL;
  if (jid>0)
    {
      eXosip_call_dialog_find(jid, &jc, &jd);
    }
  if (jd==NULL)
    {
      fprintf(stderr, "eXosip: No call here?\n");
      return;
    }
  if (status>100 && status<200)
    {
      eXosip_answer_invite_1xx(jc, jd, status);
    }
  else if (status>199 && status<300)
    {
      eXosip_answer_invite_2xx(jc, jd, status);
    }
  else if (status>300 && status<699)
    {
      eXosip_answer_invite_3456xx(jc, jd, status);
    }
  else
    {
      fprintf(stderr, "eXosip: wrong status code (101<status<699)\n");
      return;
    }
}

void eXosip_on_hold_call  (int jid)
{
  eXosip_dialog_t *jd = NULL;
  eXosip_call_t *jc = NULL;

  osip_transaction_t *transaction;
  osip_event_t *sipevent;
  osip_message_t *invite;
  int i;
  sdp_message_t *sdp;
  char *body;
  char *size;

  if (jid>0)
    {
      eXosip_call_dialog_find(jid, &jc, &jd);
    }
  if (jd==NULL)
    {
      fprintf(stderr, "eXosip: No call here?\n");
      return;
    }

  transaction = eXosip_find_last_invite(jc, jd);
  if (transaction==NULL) return;
  if (transaction->state!=ICT_TERMINATED &&
      transaction->state!=IST_TERMINATED)
    return;

  sdp = eXosip_get_local_sdp_info(transaction);
  if (sdp==NULL)
    return;
  i = sdp_message_put_on_hold(sdp);
  if (i!=0)
    {
      sdp_message_free(sdp);
      return ;
    }

  i = _eXosip_build_request_within_dialog(&invite, "INVITE", jd->d_dialog, "UDP");
  if (i!=0) {
    sdp_message_free(sdp);
    return;
  }

  i = sdp_message_to_str(sdp, &body);
  if (body!=NULL)
    {
      size= (char *)osip_malloc(7*sizeof(char));
      sprintf(size,"%i",strlen(body));
      osip_parser_set_content_length(invite, size);
      osip_free(size);
      
      osip_parser_set_body(invite, body);
      osip_free(body);
      osip_parser_set_content_type(invite, "application/sdp");
    }
  else
    osip_parser_set_content_length(invite, "0");

  if (jc->c_subject!=NULL)
    osip_parser_set_subject(invite, jc->c_subject);
  else
    osip_parser_set_subject(invite, jc->c_subject);

  i = osip_transaction_init(&transaction,
		       ICT,
		       eXosip.j_osip,
		       invite);
  if (i!=0)
    {
      /* TODO: release the j_call.. */
      msg_free(invite);
      return ;
    }
  
  
  {
    sdp_message_t *old_sdp = sdp_negotiation_ctx_get_local_sdp(jc->c_ctx);
    sdp_message_free(old_sdp);
    sdp_negotiation_ctx_set_local_sdp(jc->c_ctx, sdp);  
  }

  osip_list_add(jd->d_out_trs, transaction, 0);
  
  sipevent = osip_new_outgoing_sipmessage(invite);
  sipevent->transactionid =  transaction->transactionid;
  
  osip_transaction_add_event(transaction, sipevent);

  osip_transaction_set_your_instance(transaction, __eXosip_new_jinfo(jc, NULL, NULL, NULL));
}

void eXosip_off_hold_call (int jid)
{

}

int eXosip_create_transaction(eXosip_call_t *jc,
			     eXosip_dialog_t *jd,
			     osip_message_t *request)
{
  osip_event_t *sipevent;
  osip_transaction_t *tr;
  int i;
  i = osip_transaction_init(&tr,
		       NICT,
		       eXosip.j_osip,
		       request);
  if (i!=0)
    {
      /* TODO: release the j_call.. */

      msg_free(request);
      return -1;
    }
  
  if (jd!=NULL)
    osip_list_add(jd->d_out_trs, tr, 0);
  
  sipevent = osip_new_outgoing_sipmessage(request);
  sipevent->transactionid =  tr->transactionid;
  
  osip_transaction_add_event(tr, sipevent);
  osip_transaction_set_your_instance(tr, __eXosip_new_jinfo(jc, jd, NULL, NULL));
  return 0;
}

void eXosip_transfer_call(int jid, char *refer_to)
{
  int i;
  osip_message_t *request;
  eXosip_dialog_t *jd = NULL;
  eXosip_call_t *jc = NULL;
  if (jid<=0)
    return;

  eXosip_call_dialog_find(jid, &jc, &jd);
  if (jd==NULL || jd->d_dialog==NULL || jd->d_dialog->state==DIALOG_EARLY)
    {
      fprintf(stderr, "eXosip: No established call here!");
      return;
    }

  i = generating_refer(&request, jd->d_dialog, refer_to);
  if (i!=0)
    fprintf(stderr, "eXosip: cannot terminate this call! ");

  i = eXosip_create_transaction(jc, jd, request);
  if (i!=0)
    fprintf(stderr, "eXosip: cannot initiate SIP transfer transaction!");
}

int eXosip_create_cancel_transaction(eXosip_call_t *jc,
				    eXosip_dialog_t *jd, osip_message_t *request)
{
  osip_event_t *sipevent;
  osip_transaction_t *tr;
  int i;
  i = osip_transaction_init(&tr,
		       NICT,
		       eXosip.j_osip,
		       request);
  if (i!=0)
    {
      /* TODO: release the j_call.. */

      msg_free(request);
      return -1;
    }
  
  osip_list_add(eXosip.j_transactions, tr, 0);
  
  sipevent = osip_new_outgoing_sipmessage(request);
  sipevent->transactionid =  tr->transactionid;
  
  osip_transaction_add_event(tr, sipevent);
  return 0;
}

void eXosip_terminate_call(int cid, int jid)
{
  int i;
  osip_message_t *request;
  eXosip_dialog_t *jd = NULL;
  eXosip_call_t *jc = NULL;
  if (jid>0)
    {
      eXosip_call_dialog_find(jid, &jc, &jd);
      if (jd==NULL)
	{
	  fprintf(stderr, "eXosip: No call here? ");
	  return;
	}
    }
  else
    {
      eXosip_call_find(cid, &jc);      
    }

  if (jd==NULL || jd->d_dialog==NULL)
    {
      osip_transaction_t *tr;
      fprintf(stderr, "eXosip: No established dialog!");
      //#warning TODO: choose the latest not the first one.
      tr=jc->c_out_tr;
      if (tr!=NULL && tr->last_response!=NULL && MSG_IS_STATUS_1XX(tr->last_response))
	{
	  i = generating_cancel(&request, tr->orig_request);
	  if (i!=0)
	    fprintf(stderr, "eXosip: cannot terminate this call! ");
	  return;
	  i = eXosip_create_cancel_transaction(jc, jd, request);
	  if (i!=0)
	    fprintf(stderr, "eXosip: cannot initiate SIP transaction! ");
	}
      return;
    }
  i = generating_bye(&request, jd->d_dialog);
  if (i!=0)
    fprintf(stderr, "eXosip: cannot terminate this call! ");

  i = eXosip_create_transaction(jc, jd, request);
  if (i!=0)
    fprintf(stderr, "eXosip: cannot initiate SIP transaction! ");

  osip_dialog_free(jd->d_dialog);
  jd->d_dialog = NULL;
}

void eXosip_register      (int rid)
{
  osip_transaction_t *transaction;
  osip_event_t *sipevent;
  osip_message_t *reg;
  int i;

  eXosip_reg_t *jr;

  jr = eXosip.j_reg;
  if (jr==NULL)
    {
      fprintf(stderr, "eXosip: no registration info saved!\n");
      return ;
    }
  reg = NULL;
  if (jr->r_last_tr!=NULL)
    {
      if (jr->r_last_tr->state!=NICT_TERMINATED)
	{
	  fprintf(stderr, "eXosip: a registration is already pending!\n");
	  return ;
	}
      else
	{
	  reg = jr->r_last_tr->orig_request;
	  jr->r_last_tr->orig_request = NULL;
	  __eXosip_delete_jinfo(jr->r_last_tr);
	  osip_transaction_free2(jr->r_last_tr);
	  jr->r_last_tr = NULL;

	  /* modify the REGISTER request */
	  {
	    int osip_cseq_num = osip_atoi(reg->cseq->number);
	    int length   = strlen(reg->cseq->number);
	    char *tmp    = (char *)osip_malloc(90*sizeof(char));
	    osip_via_t *via   = (osip_via_t *) osip_list_get (reg->vias, 0);
	    osip_list_remove(reg->vias, 0);
	    osip_via_free(via);
	    sprintf(tmp, "SIP/2.0/UDP %s:%s;branch=z9hG4bK%u",
		    localip,
		    localport,
		    via_branch_new_random());
	    osip_via_init(&via);
	    osip_via_parse(via, tmp);
	    osip_list_add(reg->vias, via, 0);
	    osip_free(tmp);

	    osip_cseq_num++;
	    osip_free(reg->cseq->number);
	    reg->cseq->number = (char*)osip_malloc(length+2); /* +2 like for 9 to 10 */
	    sprintf(reg->cseq->number, "%i", osip_cseq_num);
	    
	  }
	}
    }
  if (reg==NULL)
    {
      i = generating_register(&reg, jr->r_aor, jr->r_registrar, jr->r_contact);
      if (i!=0) 
	{
	  fprintf(stderr, "eXosip: cannot register (cannot build REGISTER)! ");
	  return;
	}
    }

  i = osip_transaction_init(&transaction,
		       NICT,
		       eXosip.j_osip,
		       reg);
  if (i!=0)
    {
      /* TODO: release the j_call.. */

      msg_free(reg);
      return ;
    }

  jr->r_last_tr = transaction;

  /* send REGISTER */
  sipevent = osip_new_outgoing_sipmessage(reg);
  sipevent->transactionid =  transaction->transactionid;
  msg_force_update(reg);
  
  osip_transaction_add_event(transaction, sipevent);

}


void
eXosip_register_init(char *from, char *proxy, char *contact)
{
  eXosip_reg_t *jr;
  int i;

  i = eXosip_reg_init(&jr, from, proxy, contact);
  if (i!=0) 
    {
      fprintf(stderr, "eXosip: cannot register! ");
      return ;
    }
  eXosip.j_reg = jr;
}


void eXosip_subscribe    (char *to, char *from, char *route)
{
  eXosip_subscribe_t *js;
  osip_message_t *subscribe;
  osip_transaction_t *transaction;
  osip_event_t *sipevent;
  int i;
    
  i = generating_initial_subscribe(&subscribe, to, from, route);
  if (i!=0) 
    {
      fprintf(stderr, "eXosip: cannot subscribe (cannot build SUBSCRIBE)! ");
      return;
    }

  i = eXosip_subscribe_init(&js);
  if (i!=0)
    {
      fprintf(stderr, "eXosip: cannot subscribe.");
      return;
    }
  
  i = osip_transaction_init(&transaction,
		       NICT,
		       eXosip.j_osip,
		       subscribe);
  if (i!=0)
    {
      msg_free(subscribe);
      return ;
    }
  
  js->s_out_tr = transaction;
  
  sipevent = osip_new_outgoing_sipmessage(subscribe);
  sipevent->transactionid =  transaction->transactionid;
  
  osip_transaction_add_event(transaction, sipevent);

  osip_transaction_set_your_instance(transaction, __eXosip_new_jinfo(NULL, NULL, js, NULL));
  ADD_ELEMENT(eXosip.j_subscribes, js);
}

void eXosip_notify_send_notify(eXosip_notify_t *jn,
			       eXosip_dialog_t *jd,
			       int status)
{
  osip_transaction_t *transaction;
  osip_message_t *notify;
  osip_event_t *sipevent;
  int i;
  transaction = eXosip_find_last_out_notify(jn, jd);
  if (transaction!=NULL)
    {
      if (transaction->state!=NICT_TERMINATED &&
	  transaction->state!=NIST_TERMINATED)
	return;
    }

  i = _eXosip_build_request_within_dialog(&notify, "NOTIFY", jd->d_dialog, "UDP");
  if (i!=0) {
    return;
  }

  jn->n_online_status = status;

  i = osip_transaction_init(&transaction,
		       NICT,
		       eXosip.j_osip,
		       notify);
  if (i!=0)
    {
      /* TODO: release the j_call.. */
      msg_free(notify);
      return ;
    }
  
  osip_list_add(jd->d_out_trs, transaction, 0);
  
  sipevent = osip_new_outgoing_sipmessage(notify);
  sipevent->transactionid =  transaction->transactionid;
  
  osip_transaction_add_event(transaction, sipevent);

  osip_transaction_set_your_instance(transaction, __eXosip_new_jinfo(NULL, NULL, NULL, jn));
}

void eXosip_notify  (int nid, int status)
{
  eXosip_dialog_t *jd = NULL;
  eXosip_notify_t *jn = NULL;

  if (nid>0)
    {
      eXosip_notify_dialog_find(nid, &jn, &jd);
    }
  if (jd==NULL)
    {
      fprintf(stderr, "eXosip: No subscribe dialog here?\n");
      return;
    }

  eXosip_notify_send_notify(jn, jd, status);
}


void eXosip_notify_accept_subscribe   (int nid, int code, int status)
{
  eXosip_dialog_t *jd = NULL;
  eXosip_notify_t *jn = NULL;
  if (nid>0)
    {
      eXosip_notify_dialog_find(nid, &jn, &jd);
    }
  if (jd==NULL)
    {
      fprintf(stderr, "eXosip: No call here?\n");
      return;
    }
  if (code>100 && code<200)
    {
      eXosip_notify_answer_subscribe_1xx(jn, jd, code);
    }
  else if (code>199 && code<300)
    {
      eXosip_notify_answer_subscribe_2xx(jn, jd, code);
    }
  else if (code>300 && code<699)
    {
      eXosip_notify_answer_subscribe_3456xx(jn, jd, code);
    }
  else
    {
      fprintf(stderr, "eXosip: wrong status code (101<code<699)\n");
      return;
    }

  eXosip_notify(nid, status);
}
