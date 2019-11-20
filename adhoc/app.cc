

byte node_ID = 0;



struct msg {
	byte nodeID;
	byte pathID;
	byte hopCount;
	byte powerLVL;
}




msg node_init() {
	


	return node;
}

fsm root {

	

	state Init:

	state Init_t:
		tcv_control (sfd, PHYSOPT_SETPOWER, &power);
		tcv_control (sfd, PHYSOPT_GETPOWER, &ReadPower);
			
	
	state:

	state Sending:

}

fsm additional_send {

		
}

fsm recieve {
	address packet;
		
	state Receiving:
		packet = tcv_rnp(Receiving,sfd);

	state OK:
		struct msg* payload = (struct msg*)(packet+1);

		//switch statement
			
			//case: parent node->child nodes
			
			//case: child node->parent node->sink node

			//case: new node -> new connection				

}





