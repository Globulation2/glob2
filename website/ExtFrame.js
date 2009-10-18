Ext.onReady(function() {
	var columns=['key','en', 'ca','cz','dk','es','fr','hu','it','nl','pl','pt','ro','ru','se','si','sk','sr','sv','de','tr'];
	var CMInitializer = new Array();
	var ReaderInitializer = new Array();
	var languageTogglerInitializer = new Array();
	var KeyInitializer = new Array();
	var languages = new Array();
	for(i in columns) {
		CMInitializer=CMInitializer.concat([{
			header: columns[i],
			dataIndex: columns[i],
			width: 200,
			hidden: true,
			editor: new Ext.form.TextField()
		}]);
		ReaderInitializer=ReaderInitializer.concat([{
			name: columns[i]
		}]);
		if(i>0) {//'key' soll kein button bekommen
			languageTogglerInitializer=languageTogglerInitializer.concat([{
				text: columns[i],
				handler: toggleLanguage,
				enableToggle: true
			}]);
		}
		KeyInitializer=KeyInitializer.concat([{name: columns[i], type: 'string'}]);
	}
	//key-column special treatment
	CMInitializer[0].hidden=false;
	CMInitializer[0].width=80;
	CMInitializer[0].editor='';
	var CM = new Ext.grid.ColumnModel(CMInitializer);
	var Reader= new Ext.data.JsonReader({
			root: 'rows',
			totalProperty: 'results',
			id: 'key',
			fields: ReaderInitializer});
	var Store = new Ext.data.Store({
		reader: Reader,
		listeners: {
			load: function(store, records, options) {
				GP.render('grid-example');
			}
		},
		remoteSort: true
	});
	function refreshStore() {
		Store.proxy=new Ext.data.HttpProxy({
			url: '/translation/getTranslations.php?languages=' + languages.join(',')
		});
		Store.reload();
	}
	var GP = new Ext.grid.EditorGridPanel({
		stripeRows: true,
		id: 'GP',
		loadMask: true,
		region:'center',
		border:false,
		ds: Store,
		cm: CM,
		minSize:250,
		height:500,
		width:'100%',
		clicksToEdit:1,
		viewConfig: {
			forceFit:true
		},
		collapsible:false,
		frame:false,
		layout:'fit',
		tbar:languageTogglerInitializer,
        bbar: [
			
				new Ext.PagingToolbar({
					pageSize: 25,
					store: Store,
					emptyMsg: "No keys to display"
				})
			,'->'
			,{
				text: 'Add key',
				handler : function(){
					Ext.Msg.prompt('Key value', 'Please enter a key value:', function(btn, text){
						if (btn == 'ok'){
							var k = new Key({
								key: text
							});
							newKey(text,k);
						}
					});
				}
			},{
				text: 'store changes',
				handler : function(){
					Ext.Msg.prompt('Store version', 'All modifications done since the last store-click will be committed to our mercurial versioning system to be reviewed before inclusion to the official repository. Please leave a message including your name and mail-address if you want to be mentioned in our authors file. It is possibly also a good idea to specify the language you edited so your name is not associated with edits by others that did not store their changes to mercurial.\nYour comment:', function(btn, text){
						if (btn == 'ok'){
							Ext.Ajax.request({
								url: '/translation/commit.php',
								params: {
									comment:'languagecommit - ' + text,
									submitted: '1'
								},
								method: 'GET',
								timeout: 10000,
								waitMsg:'Executing Request...',
								scope: this,
								callback: function(options, success, response){
									if(response.responseText!=="success") {
										Ext.Msg.show({
											title:'Error:',
											msg: response.responseText
										});
									}
								}
							});
						}
					});
				}
			},{
				text: 'Add language (not implemented yet)',
				handler : function(){
					Ext.Msg.alert('no ....','not implemented yet');
				}
			}],
		listeners: {
			afteredit: function (e){
				callSetValue(e.record.get('key'),e.field,e.value);
			}
		}

	});
	function newKey(key,k) {
		Ext.Ajax.request({
			url: '/translation/setTranslation.php',
			params: {
				key:key,
				language:'key',
				newvalue:key,
				submitted: '1'
			},
			method: 'GET',
			timeout: 5000,
			waitMsg:'Executing Request...',
			scope: this,
			callback: function(options, success, response){
				if(response.responseText==="success") {
					GP.stopEditing();
					Store.insert(0, k);
					GP.startEditing(0, 0);
				} else {
					Ext.Msg.show({
						title:'Error:',
						msg: response.responseText
					});
				}
			}
		});
	}
	function callSetValue(key, language, newvalue) {
		Ext.Ajax.request({
			url: '/translation/setTranslation.php',
			params: {
				key:key,
				language:language,
				newvalue:newvalue,
				submitted: '1'
			},
			method: 'GET',
			timeout: 5000,
			waitMsg:'Executing Request...',
			scope: this,
			callback: function(options, success, response){
			}
		});
	}
	var Key = Ext.data.Record.create(KeyInitializer);
	function toggleLanguage() {
		if(this.pressed) {
			languages = languages.concat(this.text);
		} else {
			for(i in languages) {
				if(languages[i]==this.text){
					languages.splice(i,1);
				}
			}
		}
		CMindex=CM.findColumnIndex(this.text);
		CM.setEditable(CMindex,this.pressed);
		CM.setHidden(CMindex,!this.pressed);
		refreshStore();
	}
	refreshStore();
});