#include "sensor_ecpf_proc.h"

SensorEcpfProc::SensorEcpfProc(Node* anode) : node(anode)
{
	this->inode = dynamic_cast<INode_SensorEcpfProc*>(this->node);
	this->inetwork = dynamic_cast<INet_SensorEcpfProc*>(this->node->get_network());
	this->clock = this->node->get_network()->get_clock();
	this->comm = dynamic_cast<ClusteringCommProxy*>(this->node->get_commproxy());
	
	this->energy_thrd = 0.7;
	this->max_main_iter = 3;
	this->max_wait_self_time = 0.7;

	this->ecpf_time = 1;
	this->route_time = 0.5;
	this->stable_time = ClusteringSimModel::SENSE_DATA_PERIOD * 8;
	this->check_time = 0.5;
	
	this->min_tick = 0.01;
	this->main_iter_tick = 0.01;
	
	this->tents = new SortedList<ecpf::Tent>();
	this->timer = new Timer(this->node->get_network()->get_clock());
	this->wait_timer = new Timer(this->node->get_network()->get_clock());
	
	this->energy_pre = ClusteringSimModel::E_INIT;
	this->is_cluster = true;
	this->fuzzycost = -1;
	this->main_iter = -1;
}

SensorEcpfProc::~SensorEcpfProc()
{
	delete this->tents;
	this->tents = NULL;
	delete this->timer;
	this->timer = NULL;
}

void SensorEcpfProc::init()
{
	this->proc_state = SensorEcpfProc::PROC_SLEEP;
}

double SensorEcpfProc::start_clustering()
{
	this->inode->set_ch_addr(-1);
	this->inode->set_next_hop(-1);
	this->proc_state = SensorEcpfProc::PROC_GETREADY;
	this->timer->set_after(this->ecpf_time);
	return this->ecpf_time;
}

void SensorEcpfProc::exit_clustering()
{
	if(this->inode->get_ch_addr() < 0){
		this->inode->set_ch_addr(this->node->get_addr());
	}
	this->energy_pre = this->node->energy;
	this->proc_state = SensorEcpfProc::PROC_SLEEP;
	this->timer->set_after(this->route_time + this->stable_time);
}

bool SensorEcpfProc::check_energy()
{
	return this->node->energy/this->energy_pre < this->energy_thrd ||
		this->inode->get_ch_addr() < 0 ||
		this->inode->get_next_hop() < 0;
}

void SensorEcpfProc::start_check()
{
	bool f = false;
	int toaddr = this->inode->get_next_hop();
	if(toaddr < 0 || !this->inetwork->is_alive(toaddr)){
		f = true;
		toaddr = ClusteringSimModel::SINK_ADDR;
	}else if(this->inode->is_ch() && this->check_energy()){
		f = true;
	}else if(!this->inode->is_ch() && !this->inetwork->is_alive(this->inode->get_ch_addr())){
		f = true;
	}
	if(f){
		this->comm->unicast(
			this->node->get_addr(), toaddr, 
			ClusteringSimModel::CTRL_PACKET_SIZE, 
			SensorEcpfProc::CMD_NEED, 
			0, NULL);
	}
	this->is_cluster = false;
}

void SensorEcpfProc::ticktock(double time)
{
	if(this->timer->is_timeout()){
		switch(this->proc_state)
		{
		case SensorEcpfProc::PROC_SLEEP:
		{
			this->start_check();
			this->proc_state = SensorEcpfProc::PROC_CHECK;
			this->timer->set_after(this->check_time);
			this->clock->try_set_tick(this->min_tick);
			break;
		}
		case SensorEcpfProc::PROC_CHECK:
		{	
			if(this->is_cluster){
				this->start_clustering();
				this->clock->try_set_tick(this->min_tick);
			}else{
				this->proc_state = SensorEcpfProc::PROC_SLEEP;
				this->timer->set_after(this->ecpf_time + this->route_time + this->stable_time);
			}
			break;
		}
		default:
		{
			this->exit_clustering();
			this->inode->start_route();
		}
		}
	}else{
		this->clock->try_set_tick(this->timer->get_time() - this->clock->get_time());
		if(this->proc_state != SensorEcpfProc::PROC_SLEEP && this->proc_state != SensorEcpfProc::PROC_CHECK){
			this->proc_clustering();
		}
	}
	if(!this->check_ch_alive()){
		//this->inode->set_next_hop(-1);
	}
}

int SensorEcpfProc::process(Msg* msg)
{
	switch(msg->cmd)
	{
	case SensorEcpfProc::CMD_CH:
	{
		this->receive_ch_msg(msg);
		return 1;
	}
	case SensorEcpfProc::CMD_JOIN:
	{
		this->receive_join_msg(msg);
		return 1;
	}
	case SensorEcpfProc::CMD_NEED:
	{
		this->receive_need_msg();
		return 1;
	}
	case SensorEcpfProc::CMD_CLUSTER:
	{
		this->is_cluster = true;
		return 1;
	}
	}
	return 0;
}

int SensorEcpfProc::proc_clustering()
{	
	switch(this->proc_state)
	{
	case SensorEcpfProc::PROC_SLEEP:
	{
		break;
	}
	case SensorEcpfProc::PROC_DONE:
	{
		break;
	}
	case SensorEcpfProc::PROC_GETREADY:
	{
		this->tents->clear();
		this->calFuzzyCost();
		this->ch_type = SensorEcpfProc::NOT_CH;
		this->wait_timer->set_after(this->calDelay());
		this->proc_state = SensorEcpfProc::PROC_WAIT;
		this->clock->try_set_tick(this->wait_timer->get_time() - this->clock->get_time());
		break;
	}
	case SensorEcpfProc::PROC_WAIT:
	{
		if(this->wait_timer->is_timeout()){
			this->proc_state = SensorEcpfProc::PROC_MAIN;
			this->main_iter = 0;
		}else{
			this->clock->try_set_tick(this->wait_timer->get_time() - this->clock->get_time());
		}
		break;
	}
	case SensorEcpfProc::PROC_MAIN:
	{
		if( this->main_iter < this->max_main_iter )
		{
			if(this->has_sch())
			{
				if(!this->has_finals())
				{
					int lcost_ch = this->get_least_cost_ch();
					if( lcost_ch == this->node->get_addr() )
					{
						this->ch_type = SensorEcpfProc::FINAL_CH;
						//this->node->energy = 800;
						this->add_tent(this->node->get_addr(), this->ch_type, this->fuzzycost);
						this->broadcast_ch_msg();
						this->inode->set_ch_addr(this->node->get_addr());
					}
				}
				else
				{
					this->main_iter = this->max_main_iter;
				}
			}
			else
			{
				this->ch_type = SensorEcpfProc::TENT_CH;
				this->add_tent(this->node->get_addr(), this->ch_type, this->fuzzycost);
				this->broadcast_ch_msg();
				this->inode->set_ch_addr(this->node->get_addr());
			}
			this->main_iter++;
		}
		else
		{
			this->proc_state = SensorEcpfProc::PROC_FINAL;
		}
		this->node->get_network()->get_clock()->try_set_tick(this->min_tick);
		break;
	}
	case SensorEcpfProc::PROC_FINAL:
	{
		if( this->ch_type != SensorEcpfProc::FINAL_CH )
		{
			int lcost_final_ch = this->get_least_cost_final_ch();
			if( lcost_final_ch >= 0 )
			{
				this->join_cluster(lcost_final_ch);
				this->inode->set_ch_addr(lcost_final_ch);
			}
			else
			{
				this->ch_type = SensorEcpfProc::FINAL_CH;
				//this->node->energy = 400;
				this->add_tent(this->node->get_addr(), this->ch_type, this->fuzzycost);
				this->broadcast_ch_msg();
			}
		}
		if( this->ch_type == SensorEcpfProc::FINAL_CH )
		{
			this->inode->set_ch_addr(this->node->get_addr());
		}
		this->proc_state = SensorEcpfProc::PROC_DONE;
		this->node->get_network()->get_clock()->try_set_tick(this->min_tick);
		break;
	}
	}
	return 1;
}

void SensorEcpfProc::receive_ch_msg(Msg* msg)
{
	this->add_tent(msg->fromaddr, msg->data[0], *(double*)(msg->data + sizeof(char)));
}

void SensorEcpfProc::receive_join_msg(Msg* msg)
{
	this->inode->set_ch_addr(this->node->get_addr());
	this->add_member(msg->fromaddr);
}

void SensorEcpfProc::receive_need_msg()
{
	int toaddr = this->inode->get_next_hop();
	if(toaddr < 0 || !this->inetwork->is_alive(toaddr)){
		toaddr = ClusteringSimModel::SINK_ADDR;
	}
	this->comm->unicast(
		this->node->get_addr(), toaddr, 
		ClusteringSimModel::CTRL_PACKET_SIZE, 
		SensorEcpfProc::CMD_NEED, 
		0, NULL);
}

void SensorEcpfProc::add_member(int addr)
{
	this->inode->get_mnmanager()->add(addr);
}

void SensorEcpfProc::add_tent(int addr, char type, double cost)
{
	ecpf::Tent* sc = this->tents->find(ecpf::Tent(addr, 0, 0x00));
	if(sc == NULL){
		this->tents->add(new ecpf::Tent(addr, cost, type));
	}else{
		sc->type = type;
	}
}

double SensorEcpfProc::calDelay()
{
	//return 1 / std::max(this->node->energy / ClusteringSimModel::E_INIT, 0.01) / 100;
	//return (1 - this->node->energy / ClusteringSimModel::E_INIT)  * this->max_wait_self_time + this->min_tick;
	//return (1 - this->node->energy / ClusteringSimModel::E_INIT * rand() / (RAND_MAX + 1.0)) * this->max_wait_self_time;
	return std::min(0.07 / (this->node->energy / ClusteringSimModel::E_INIT), this->max_wait_self_time);
}

double SensorEcpfProc::calFuzzyCost()
{
	double s = 0;
	int c = 0;
	double d;
	NgbManager* ngbs = this->inode->get_neighbors();
	ngbs->seek(0);
	while(ngbs->has_more()){
		d = ngbs->next()->d;
		if(d <= ClusteringSimModel::CLUSTER_RADIUS){
			c ++;
			s += pow(d, 2);
		}
	}
	double centrality = sqrt(s / c)/ (ClusteringSimModel::AREA_SIZE_X + ClusteringSimModel::AREA_SIZE_Y) * 2.0;
	double degree = c / ClusteringSimModel::NODE_NUM;
	//this->fuzzycost = centrality / degree;
	//this->fuzzycost = rand() / (RAND_MAX + 1.0);
	this->fuzzycost = ecpf::fcc->cal(centrality, degree);
	return this->fuzzycost;
}

void SensorEcpfProc::broadcast_ch_msg()
{
	if(this->ch_type == SensorEcpfProc::TENT_CH || this->ch_type == SensorEcpfProc::FINAL_CH){
		int data_l = sizeof(char) + sizeof(double);
		char *data = new char[data_l];
		data[0] = this->ch_type;
		*(double*)(data + sizeof(char)) = this->fuzzycost;
		this->comm->broadcast(
			this->node->get_addr(), ClusteringSimModel::CLUSTER_RADIUS, 
			ClusteringSimModel::CTRL_PACKET_SIZE, 
			SensorEcpfProc::CMD_CH, 
			data_l, data);
		delete[] data;
	}
}

int SensorEcpfProc::get_least_cost_ch()
{
	ecpf::Tent* sc = NULL;
	this->tents->seek(0);
	while(this->tents->has_more()){
		sc = this->tents->next();
		if(sc->type == SensorEcpfProc::TENT_CH || sc->type == SensorEcpfProc::FINAL_CH){
			return sc->addr;
		}
	}
	return -1;
}

int SensorEcpfProc::get_least_cost_final_ch()
{
	ecpf::Tent* sc = NULL;
	this->tents->seek(0);
	while(this->tents->has_more()){
		sc = this->tents->next();
		if(sc->type == SensorEcpfProc::FINAL_CH){
			return sc->addr;
		}
	}
	return -1;
}

void SensorEcpfProc::join_cluster(int ch_addr)
{
	if(this->inode->is_ch()){
		return;
	}
	this->ch_type = SensorEcpfProc::NOT_CH;
	this->inode->set_ch_addr(ch_addr);
	this->comm->unicast(
		this->node->get_addr(), ch_addr, 
		ClusteringSimModel::CTRL_PACKET_SIZE, 
		SensorEcpfProc::CMD_JOIN, 
		0, NULL);
}

bool SensorEcpfProc::has_finals()
{
	ecpf::Tent* sc = NULL;
	this->tents->seek(0);
	while(this->tents->has_more()){
		sc = this->tents->next();
		if(sc->type == SensorEcpfProc::FINAL_CH){
			return true;
		}
	}
	return false;
}

bool SensorEcpfProc::has_sch()
{
	ecpf::Tent* sc = NULL;
	this->tents->seek(0);
	while(this->tents->has_more()){
		sc = this->tents->next();
		if(sc->type == SensorEcpfProc::TENT_CH || sc->type == SensorEcpfProc::FINAL_CH){
			return true;
		}
	}
	return false;
}

bool SensorEcpfProc::check_ch_alive()
{
	if(this->inode->get_ch_addr() < 0){
		return false;
	}
	return this->inetwork->is_alive(this->inode->get_ch_addr());	
}
	
ecpf::FuzzyCostComputor::FuzzyCostComputor()
{
	this->engine = new fl::Engine;
	this->engine->setName("FuzzyCostComputor");
	
	this->degree = new fl::InputVariable;
	degree->setEnabled(true);
	degree->setName("degree");
	degree->setRange(0.000, 1.000);

	double ad = ClusteringSimModel::NODE_NUM / 
		(ClusteringSimModel::AREA_SIZE_X * ClusteringSimModel::AREA_SIZE_Y / pow(ClusteringSimModel::CLUSTER_RADIUS, 2) / 3.14);
	double md = ad*3/2;
	double ld = ad/2;
	degree->addTerm(new fl::Trapezoid("low", 0.000, 0.000, ld, ad));
	degree->addTerm(new fl::Triangle("med", ld, ad, md));
	degree->addTerm(new fl::Trapezoid("high", ad, md, 1.000, 1.000));
	this->engine->addInputVariable(degree);
	
	this->centrality = new fl::InputVariable;
	centrality->setEnabled(true);
	centrality->setName("centrality");
	centrality->setRange(0.000, 1.000);

	double mc = ClusteringSimModel::CLUSTER_RADIUS / (ClusteringSimModel::AREA_SIZE_X + ClusteringSimModel::AREA_SIZE_Y) * 2;
	double ac = mc*2/3;
	double lc = ac/2;
	centrality->addTerm(new fl::Trapezoid("close", 0.000, 0.000, lc, ac));
	centrality->addTerm(new fl::Triangle("adequate", lc, ac, mc));
	centrality->addTerm(new fl::Trapezoid("far", ac, mc, 1.000, 1.000));
	this->engine->addInputVariable(centrality);
	
	this->cost = new fl::OutputVariable;
	cost->setEnabled(true);
	cost->setName("cost");
	cost->setRange(0.000, 100.0);
	cost->fuzzyOutput()->setAccumulation(new fl::Maximum);
	cost->setDefuzzifier(new fl::Centroid(100));
	cost->setDefaultValue(fl::nan);
	cost->setLockValidOutput(false);
	cost->setLockOutputRange(false);

	cost->addTerm(new fl::Trapezoid("vl", 0.000, 0.000, 5.000, 10.00));
	cost->addTerm(new fl::Triangle("l", 0.000, 10.00, 20.00));
	cost->addTerm(new fl::Triangle("rl", 10.00, 22.50, 35.00));
	cost->addTerm(new fl::Triangle("ml", 20.00, 35.00, 45.00));
	cost->addTerm(new fl::Triangle("m", 20.00, 45.00, 70.00));
	cost->addTerm(new fl::Triangle("mh", 45.00, 55.00, 70.00));
	cost->addTerm(new fl::Triangle("rh", 55.00, 70.00, 80.00));
	cost->addTerm(new fl::Triangle("h", 70.00, 80.00, 100.0));
	cost->addTerm(new fl::Trapezoid("vh", 80.00, 95.00, 100.0, 100.0));
	this->engine->addOutputVariable(cost);
	
	this->rules = new fl::RuleBlock;
	this->rules->setEnabled(true);
	this->rules->setName("");
	this->rules->setConjunction(NULL);
	this->rules->setDisjunction(NULL);
	this->rules->setActivation(new fl::Minimum);

	this->rules->addRule(fl::Rule::parse("if centrality is close and degree is high then cost is vl", this->engine));
	this->rules->addRule(fl::Rule::parse("if centrality is close and degree is med then cost is l", this->engine));
	this->rules->addRule(fl::Rule::parse("if centrality is close and degree is low then cost is rl", this->engine));
	this->rules->addRule(fl::Rule::parse("if centrality is adequate and degree is high then cost is ml", this->engine));
	this->rules->addRule(fl::Rule::parse("if centrality is adequate and degree is med then cost is m", this->engine));
	this->rules->addRule(fl::Rule::parse("if centrality is adequate and degree is low then cost is mh", this->engine));
	this->rules->addRule(fl::Rule::parse("if centrality is far and degree is high then cost is rh", this->engine));
	this->rules->addRule(fl::Rule::parse("if centrality is far and degree is med then cost is h", this->engine));
	this->rules->addRule(fl::Rule::parse("if centrality is far and degree is low then cost is vh", this->engine));
	this->engine->addRuleBlock(this->rules);
	
	this->engine->configure();
	
	std::string status;
	if (not engine->isReady(&status)){
		throw fl::Exception("Engine not ready. "
            "The following errors were encountered:\n" + status, FL_AT);
	}
}

ecpf::FuzzyCostComputor::~FuzzyCostComputor()
{
	delete this->engine;
	this->engine = NULL;
	delete this->centrality;
	this->centrality = NULL;
	delete this->degree;
	this->degree = NULL;
	delete this->rules;
	this->rules = NULL;
}

double ecpf::FuzzyCostComputor::cal(double centrality, double degree)
{ 
	fl::scalar c = centrality;
	fl::scalar d = degree;
	this->centrality->setInputValue(c);
	this->degree->setInputValue(d);
	this->engine->process();
	double res = this->cost->defuzzify();
	return res;
}

namespace ecpf
{
	FuzzyCostComputor* fcc = new FuzzyCostComputor();
}