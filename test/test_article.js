var $ = require('jquery');

exports.test_get_top_10_article_list = function(test) {
	test.done();
};

exports.test_get_announce_article_list_if_announce_exist = function(test) {
	var url = 'http://extdev.ironblood.net:8080/article/list?type=announce';
	$.getJSON(url, function(data) {
		if(data.errcode == 0 && data.articlelist.length == 3) {
			test.ok(true, "get announce successfully.");
			/*data.articlelist.forEach(function(entry) {
				console.log(entry);
			});*/
			test.done();
		} else {
			test.ok(false, "get announce failed. errcode: " + data.errcode);
			console.log(data.articlelist);
			test.done();
		}
	});
};

exports.test_get_commend_article_list_if_commend_exist = function(test) {
	var url = 'http://extdev.ironblood.net:8080/article/list?type=commend';
	$.getJSON(url, function(data) {
		if(data.errcode == 0 && data.articlelist.length == 3) {
			test.ok(true, "get commend successfully.");
			/*data.articlelist.forEach(function(entry) {
				console.log(entry);
			});*/
			test.done();
		} else {
			test.ok(false, "get commend failed. errcode: " + data.errcode);
			console.log(data.articlelist);
			test.done();
		}
	});
};

exports.test_get_the_second_annouce_entry_with_startnum_and_count = function(test) {
	var url = 'http://extdev.ironblood.net:8080/article/list?type=announce&startnum=2&count=1';
	$.getJSON(url, function(data) {
		if(data.errcode == 0 && data.articlelist.length == 1) {
			test.equal(data.articlelist[0].type, 0, "data tpye error");
			test.equal(data.articlelist[0].author, "test", "data author error");
			test.equal(data.articlelist[0].board, "sysop", "data board error");
			test.equal(data.articlelist[0].aid, 1383455735, "data id error expect " + 1383455735 + " actually " + data.articlelist[0].aid);
			test.equal(data.articlelist[0].title, "公告2", "data title error");
			test.done();
		} else {
			test.ok(false, "get announce failed. errcode: " + data.errcode);
			//console.log(data.articlelist);
			test.done();
		}
	});
};

exports.test_get_the_second_commend_entry_with_startnum_and_count = function(test) {
	var url = 'http://extdev.ironblood.net:8080/article/list?type=commend&startnum=2&count=1';
	$.getJSON(url, function(data) {
		if(data.errcode == 0 && data.articlelist.length == 1) {
			test.equal(data.articlelist[0].type, 0, "data tpye error");
			test.equal(data.articlelist[0].author, "test", "data author error");
			test.equal(data.articlelist[0].board, "sysop", "data board error");
			test.equal(data.articlelist[0].aid, 1383455710, "data id error expect " + 1383455710 + " actually " + data.articlelist[0].aid);
			test.equal(data.articlelist[0].title, "美文2", "data title error");
			test.done();
		} else {
			test.ok(false, "get announce failed. errcode: " + data.errcode);
			//console.log(data.articlelist);
			test.done();
		}
	});
};
